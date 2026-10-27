#include "door.h"

#include "actuators.h"
#include "board_pins.h"
#include "esp_timer.h"

#define ANGLE_CLOSED 0
#define ANGLE_OPEN   90

typedef struct {
    ledc_channel_t     ch;
    int                gpio;
    esp_timer_handle_t close_timer;
} door_t;

static door_t s_doors[SIDE_COUNT] = {
    [SIDE_IN]  = { PWM_CH_DOOR_IN,  PIN_DOOR_IN_SERVO  },
    [SIDE_OUT] = { PWM_CH_DOOR_OUT, PIN_DOOR_OUT_SERVO },
};
static volatile bool s_held;

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
    esp_timer_stop(d->close_timer);                 /* re-opening restarts the countdown */
    esp_timer_start_once(d->close_timer, (uint64_t)ms * 1000);
}

void door_hold_all(bool hold)
{
    s_held = hold;
    for (int i = 0; i < SIDE_COUNT; i++) {
        esp_timer_stop(s_doors[i].close_timer);
        servo_set_angle(s_doors[i].ch, hold ? ANGLE_OPEN : ANGLE_CLOSED);
    }
}

bool door_is_held(void)
{
    return s_held;
}
