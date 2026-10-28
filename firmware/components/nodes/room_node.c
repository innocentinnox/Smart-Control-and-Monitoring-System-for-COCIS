/* Lecture-room terminal task (see room_node.h).
 *
 * One task per room, looping every SENSE_PERIOD_MS (200 ms = 5 Hz):
 *   1. drain light-mode overrides (MSG_LIGHT_CMD) sent by the hub,
 *   2. read the sensors: PIR motion and LDR light level,
 *   3. step the pure lighting logic (room_logic_step) -> wanted lamp level,
 *   4. move the real lamp one ramp step towards it (soft fade),
 *   5. report the state to the hub when something changed (or every 60 s),
 *   6. send a heartbeat every 5 s so the hub knows the room is alive,
 *   7. sleep until the next period.
 *
 * The room decides its own lights; the hub is only told about it. So if the
 * hub (or the link) fails, the room keeps working on its own.
 */
#include "room_node.h"

#include <stdio.h>

#include "actuators.h"
#include "board_pins.h"
#include "bus.h"
#include "clock.h"
#include "esp_log.h"
#include "light_sensor.h"
#include "room_logic.h"
#include "sdkconfig.h"
#include "task_config.h"

static const char *TAG = "room";

/* SENSE_PERIOD_MS: one loop of the room task = 5 Hz sensing, ms
 * HEARTBEAT_MS:    "still alive" message to the hub, ms (hub timeout 15 s) */
#define SENSE_PERIOD_MS     200
#define HEARTBEAT_MS        5000
#define STATE_REFRESH_MS    60000       /* resend state now and then: energy totals */
#define RAMP_STEP           25          /* % per sense period: 0->100 in 0.8 s */

/* Hardware and runtime info of one room terminal. */
typedef struct {
    int            room;                /* room number, 1..ROOM_COUNT */
    gpio_num_t     pir;                 /* PIR motion sensor input pin */
    int            ldr_gpio;            /* LDR pin (must be an ADC1 pin) */
    ledc_channel_t lamp;                /* PWM channel driving the lamp */
    int            lamp_gpio;           /* lamp output pin */
    int            ldr;                 /* light_sensor handle */
    room_hours_fn  in_hours;            /* "building open now?" callback */
    /* false = simulated link failure (room_node_set_link). volatile:
     * written by the CLI task, read by the room task. */
    volatile bool  link_up;
} room_node_t;

static room_node_t s_nodes[ROOM_COUNT] = {
    { .room = 1, .pir = PIN_ROOM1_PIR, .ldr_gpio = PIN_ROOM1_LDR, .lamp = PWM_CH_ROOM1_LIGHT, .lamp_gpio = PIN_ROOM1_LIGHT, .link_up = true },
    { .room = 2, .pir = PIN_ROOM2_PIR, .ldr_gpio = PIN_ROOM2_LDR, .lamp = PWM_CH_ROOM2_LIGHT, .lamp_gpio = PIN_ROOM2_LIGHT, .link_up = true },
};

/* Lighting rules for room_logic, from Kconfig (menuconfig "CoCIS Smart
 * Control"): vacant after grace_ms without motion, dim to dim_level (30 %)
 * for the last warn_ms as a warning, lamp needed at or below dark_lux, not
 * needed at or above bright_lux, lamp_watts for the energy estimate. */
static const room_logic_cfg_t CFG = {
    .grace_ms   = CONFIG_COCIS_ROOM_GRACE_S * 1000,
    .warn_ms    = CONFIG_COCIS_ROOM_WARN_S * 1000,
    .dark_lux   = CONFIG_COCIS_DARK_LUX,
    .bright_lux = CONFIG_COCIS_BRIGHT_LUX,
    .dim_level  = 30,
    .lamp_watts = CONFIG_COCIS_LAMP_WATTS,
};

/* Send one message from this room to the hub, unless the link is "cut"
 * for the node-failure demo (then the message is simply not sent).
 * Wait time 0: if the hub inbox is full the message is dropped rather than
 * delaying the lights; the next report or heartbeat will follow anyway. */
static void send_to_hub(room_node_t *n, bus_msg_t *m)
{
    if (n->link_up) {
        bus_send(room_to_node(n->room), NODE_HUB, m, 0);   /* never stall the sensing loop */
    }
}

/* Send the room's full state (MSG_ROOM_STATE) to the hub.
 * @param n     this room
 * @param r     lighting logic state (occupied, daylight, level, energy)
 * @param mode  current light mode (AUTO / manual ON / OFF)
 * @param lux   latest light reading, lux
 * Energy is kept as double in room_logic and sent as float (enough
 * precision for a display/log value, and a smaller message). */
static void report_state(room_node_t *n, const room_logic_t *r, light_mode_t mode, uint16_t lux)
{
    bus_msg_t m = {
        .type = MSG_ROOM_STATE,
        .room = {
            .occupied = r->occupied, .daylight = r->daylight, .level = r->level, .lux = lux, .mode = mode,
            .on_wh = (float)r->on_wh, .saved_wh = (float)room_logic_saved_wh(r),
        },
    };
    send_to_hub(n, &m);
}

/* The room task body (never returns).
 * @param arg  this room's room_node_t (passed by xTaskCreate) */
static void room_task(void *arg)
{
    room_node_t *n = arg;
    node_id_t self = room_to_node(n->room);
    room_logic_t logic;
    light_mode_t mode = LIGHT_AUTO;
    uint32_t now = clock_ms();
    /* Both deadlines start "now", so the first loop sends a heartbeat and
     * a state report straight away. */
    uint32_t next_hb = now, next_refresh = now;
    uint8_t shown = 0;                                  /* lamp level on the pin, ramps to logic.level */

    room_logic_init(&logic, now);
    report_state(n, &logic, mode, 0);                   /* announce ourselves */

    while (1) {
        /* Step 1: read ALL pending hub commands without waiting (timeout 0);
         * only the newest mode matters. */
        bus_msg_t cmd;
        bool mode_changed = false;
        while (bus_recv(self, &cmd, 0)) {               /* drain overrides from the hub */
            if (cmd.type == MSG_LIGHT_CMD && cmd.light.mode != mode) {
                mode = cmd.light.mode;
                mode_changed = true;
            }
        }

        /* Step 2: sensors. A failed light reading (-1) counts as 0 lux,
         * i.e. "dark": better to light the room than to leave it dark. */
        int lux = light_sensor_read_lux(n->ldr);
        room_inputs_t in = {
            .motion   = gpio_get_level(n->pir) == 1,
            .lux      = lux < 0 ? 0 : (uint16_t)lux,
            .mode     = mode,
            .in_hours = n->in_hours(),
            .now_ms   = clock_ms(),
        };
        /* Step 3: pure logic decides occupancy, daylight and the wanted
         * lamp level, and returns which of them changed (bit flags). */
        unsigned changed = room_logic_step(&logic, &CFG, &in);

        /* Step 4: move the real lamp at most RAMP_STEP % per period towards
         * the wanted level (clamp diff to -RAMP_STEP..+RAMP_STEP), so it
         * fades instead of jumping. */
        if (shown != logic.level) {                     /* soft fade, one step per period */
            int diff = (int)logic.level - shown;
            shown += diff > RAMP_STEP ? RAMP_STEP : diff < -RAMP_STEP ? -RAMP_STEP : diff;
            lamp_set(n->lamp, shown);
        }
        /* Step 5: report on any change, plus a periodic refresh so the
         * hub's energy totals stay current even in a quiet room. */
        if (changed || mode_changed || time_reached_ms(in.now_ms, next_refresh)) {
            report_state(n, &logic, mode, in.lux);
            next_refresh = in.now_ms + STATE_REFRESH_MS;
        }
        /* Step 6: heartbeat. */
        if (time_reached_ms(in.now_ms, next_hb)) {
            bus_msg_t hb = { .type = MSG_HEARTBEAT, .heartbeat.uptime_s = in.now_ms / 1000 };
            send_to_hub(n, &hb);
            next_hb = in.now_ms + HEARTBEAT_MS;
        }
        /* Step 7: vTaskDelay() blocks this task (no CPU used) for the given
         * number of FreeRTOS ticks; pdMS_TO_TICKS() converts ms to ticks.
         * The loop period is therefore about 200 ms plus the work time. */
        vTaskDelay(pdMS_TO_TICKS(SENSE_PERIOD_MS));
    }
}

void room_node_start(int room, room_hours_fn in_hours)
{
    room_node_t *n = &s_nodes[room - 1];
    n->in_hours = in_hours;

    /* PIR output is active high; the pull-down keeps the pin at 0 (no
     * motion) if the sensor is disconnected. */
    gpio_config_t pir = { .pin_bit_mask = 1ULL << n->pir, .mode = GPIO_MODE_INPUT,
                          .pull_down_en = GPIO_PULLDOWN_ENABLE };
    ESP_ERROR_CHECK(gpio_config(&pir));
    lamp_setup(n->lamp, n->lamp_gpio);
    /* A bad LDR pin is not fatal: the room still runs, with readings
     * treated as dark (see step 2 of room_task). */
    n->ldr = light_sensor_add(n->ldr_gpio);
    if (n->ldr < 0) {
        ESP_LOGE(TAG, "Room %d: LDR on GPIO %d is not an ADC1 pin", room, n->ldr_gpio);
    }

    /* Task name "room1" / "room2" (shown in FreeRTOS debug output). The
     * task gets a pointer to its own s_nodes entry as its argument. */
    char name[12];
    snprintf(name, sizeof(name), "room%d", room);
    xTaskCreate(room_task, name, TASK_ROOM_STACK, n, TASK_ROOM_PRIO, NULL);
}

void room_node_set_link(int room, bool up)
{
    if (room >= 1 && room <= ROOM_COUNT) {
        s_nodes[room - 1].link_up = up;
    }
}
