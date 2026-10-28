/* Entrance door servos with automatic close (see door.h).
 *
 * Design idea: opening a door must never block the hub task for the 3 s the
 * door stays open. So door_open() only moves the servo and arms a one-shot
 * esp_timer; when the timer expires, close_cb() moves the servo back.
 *
 * esp_timer: ESP-IDF's high-resolution software timer. A one-shot timer
 * calls its callback once, `us` microseconds after esp_timer_start_once().
 * The callback runs in the esp_timer service task (not in our task), so it
 * must be short and must not block.
 */
#include "door.h"

#include "actuators.h"
#include "board_pins.h"
#include "esp_timer.h"

#define ANGLE_CLOSED 0
#define ANGLE_OPEN   90

/* One door: its servo PWM channel, servo pin and auto-close timer. */
typedef struct {
    ledc_channel_t     ch;
    int                gpio;
    esp_timer_handle_t close_timer;
} door_t;

/* Indexed by door_side_t. close_timer is created later in door_init(). */
static door_t s_doors[SIDE_COUNT] = {
    [SIDE_IN]  = { PWM_CH_DOOR_IN,  PIN_DOOR_IN_SERVO  },
    [SIDE_OUT] = { PWM_CH_DOOR_OUT, PIN_DOOR_OUT_SERVO },
};
/* true during a break-glass emergency. volatile: written by the hub task,
 * read by the esp_timer task in close_cb(). */
static volatile bool s_held;

/* Auto-close callback (runs in the esp_timer task).
 * @param arg  the door_t of the door to close (set in door_init()).
 * During an emergency hold the door must stay open, so it is left alone. */
static void close_cb(void *arg)
{
    if (!s_held) {
        servo_set_angle(((door_t *)arg)->ch, ANGLE_CLOSED);
    }
}

void door_init(void)
{
    for (int i = 0; i < SIDE_COUNT; i++) {
        servo_setup(s_doors[i].ch, s_doors[i].gpio);
        /* One timer per door, each passing its own door_t to close_cb(),
         * so one callback serves both doors. */
        esp_timer_create_args_t args = {
            .callback = close_cb,
            .arg      = &s_doors[i],
            .name     = i == SIDE_IN ? "door_in" : "door_out",
        };
        ESP_ERROR_CHECK(esp_timer_create(&args, &s_doors[i].close_timer));
    }
}

void door_open(door_side_t side, uint32_t ms)
{
    door_t *d = &s_doors[side];
    servo_set_angle(d->ch, ANGLE_OPEN);
    /* esp_timer_start_once() fails on a timer that is already running, so
     * stop it first (stopping an idle timer is harmless). */
    esp_timer_stop(d->close_timer);                 /* re-opening restarts the countdown */
    /* esp_timer works in microseconds: ms * 1000. */
    esp_timer_start_once(d->close_timer, (uint64_t)ms * 1000);
}

void door_hold_all(bool hold)
{
    /* Set the flag first so a timer that fires right now sees it. */
    s_held = hold;
    for (int i = 0; i < SIDE_COUNT; i++) {
        /* Cancel any pending auto-close, then force the door to the
         * emergency position (open) or back to normal (closed). */
        esp_timer_stop(s_doors[i].close_timer);
        servo_set_angle(s_doors[i].ch, hold ? ANGLE_OPEN : ANGLE_CLOSED);
    }
}

bool door_is_held(void)
{
    return s_held;
}
