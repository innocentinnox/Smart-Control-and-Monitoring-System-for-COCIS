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

#define SENSE_PERIOD_MS     200
#define HEARTBEAT_MS        5000
#define STATE_REFRESH_MS    60000       /* resend state now and then: energy totals */
#define RAMP_STEP           25          /* % per sense period: 0->100 in 0.8 s */

typedef struct {
    int            room;
    gpio_num_t     pir;
    int            ldr_gpio;
    ledc_channel_t lamp;
    int            lamp_gpio;
    int            ldr;                 /* light_sensor handle */
    room_hours_fn  in_hours;
    volatile bool  link_up;
} room_node_t;

static room_node_t s_nodes[ROOM_COUNT] = {
    { .room = 1, .pir = PIN_ROOM1_PIR, .ldr_gpio = PIN_ROOM1_LDR, .lamp = PWM_CH_ROOM1_LIGHT, .lamp_gpio = PIN_ROOM1_LIGHT, .link_up = true },
    { .room = 2, .pir = PIN_ROOM2_PIR, .ldr_gpio = PIN_ROOM2_LDR, .lamp = PWM_CH_ROOM2_LIGHT, .lamp_gpio = PIN_ROOM2_LIGHT, .link_up = true },
};

static const room_logic_cfg_t CFG = {
    .grace_ms   = CONFIG_COCIS_ROOM_GRACE_S * 1000,
    .warn_ms    = CONFIG_COCIS_ROOM_WARN_S * 1000,
    .dark_lux   = CONFIG_COCIS_DARK_LUX,
    .bright_lux = CONFIG_COCIS_BRIGHT_LUX,
    .dim_level  = 30,
    .lamp_watts = CONFIG_COCIS_LAMP_WATTS,
};

static void send_to_hub(room_node_t *n, bus_msg_t *m)
{
    if (n->link_up) {
        bus_send(room_to_node(n->room), NODE_HUB, m, 0);   /* never stall the sensing loop */
    }
}

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

static void room_task(void *arg)
{
    room_node_t *n = arg;
    node_id_t self = room_to_node(n->room);
    room_logic_t logic;
    light_mode_t mode = LIGHT_AUTO;
    uint32_t now = clock_ms();
    uint32_t next_hb = now, next_refresh = now;
    uint8_t shown = 0;                                  /* lamp level on the pin, ramps to logic.level */

    room_logic_init(&logic, now);
    report_state(n, &logic, mode, 0);                   /* announce ourselves */

    while (1) {
        bus_msg_t cmd;
        bool mode_changed = false;
        while (bus_recv(self, &cmd, 0)) {               /* drain overrides from the hub */
            if (cmd.type == MSG_LIGHT_CMD && cmd.light.mode != mode) {
                mode = cmd.light.mode;
                mode_changed = true;
            }
        }

        int lux = light_sensor_read_lux(n->ldr);
        room_inputs_t in = {
            .motion   = gpio_get_level(n->pir) == 1,
            .lux      = lux < 0 ? 0 : (uint16_t)lux,
            .mode     = mode,
            .in_hours = n->in_hours(),
            .now_ms   = clock_ms(),
        };
        unsigned changed = room_logic_step(&logic, &CFG, &in);

        if (shown != logic.level) {                     /* soft fade, one step per period */
            int diff = (int)logic.level - shown;
            shown += diff > RAMP_STEP ? RAMP_STEP : diff < -RAMP_STEP ? -RAMP_STEP : diff;
            lamp_set(n->lamp, shown);
        }
        if (changed || mode_changed || time_reached_ms(in.now_ms, next_refresh)) {
            report_state(n, &logic, mode, in.lux);
            next_refresh = in.now_ms + STATE_REFRESH_MS;
        }
        if (time_reached_ms(in.now_ms, next_hb)) {
            bus_msg_t hb = { .type = MSG_HEARTBEAT, .heartbeat.uptime_s = in.now_ms / 1000 };
            send_to_hub(n, &hb);
            next_hb = in.now_ms + HEARTBEAT_MS;
        }
        vTaskDelay(pdMS_TO_TICKS(SENSE_PERIOD_MS));
    }
}

void room_node_start(int room, room_hours_fn in_hours)
{
    room_node_t *n = &s_nodes[room - 1];
    n->in_hours = in_hours;

    gpio_config_t pir = { .pin_bit_mask = 1ULL << n->pir, .mode = GPIO_MODE_INPUT,
                          .pull_down_en = GPIO_PULLDOWN_ENABLE };
    ESP_ERROR_CHECK(gpio_config(&pir));
    lamp_setup(n->lamp, n->lamp_gpio);
    n->ldr = light_sensor_add(n->ldr_gpio);
    if (n->ldr < 0) {
        ESP_LOGE(TAG, "Room %d: LDR on GPIO %d is not an ADC1 pin", room, n->ldr_gpio);
    }

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
