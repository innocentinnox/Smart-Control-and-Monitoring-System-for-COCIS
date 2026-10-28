#include "ui.h"

#include <stdio.h>
#include <string.h>

#include "actuators.h"
#include "board_pins.h"
#include "clock.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "lcd2004.h"
#include "task_config.h"

static const char *TAG = "ui";

#define REFRESH_MS     500
#define LED_ON_MS      2000

typedef enum { CMD_MESSAGE, CMD_SOUND } cmd_kind_t;

typedef struct {
    cmd_kind_t kind;
    union {
        struct { char l1[LCD_COLS + 1]; char l2[LCD_COLS + 1]; uint32_t ms; } msg;
        ui_sound_t sound;
    };
} ui_cmd_t;

static QueueHandle_t s_queue;
static ui_status_fn  s_provider;
static bool          s_lcd_ok;

/* ui-task-only state */
static char     s_shown[LCD_ROWS][LCD_COLS + 1];    /* what is on the glass now */
static char     s_msg[2][LCD_COLS + 1];
static uint32_t s_msg_until, s_led_until;

/* ---------------- Drawing ---------------- */

/* Only send a row over I2C when its text changed: no flicker, less bus use. */
static void draw_row(int row, const char *text)
{
    char padded[LCD_COLS + 1];
    snprintf(padded, sizeof(padded), "%-20.20s", text);
    if (strcmp(padded, s_shown[row]) != 0) {
        strcpy(s_shown[row], padded);
        if (s_lcd_ok) {
            lcd_print_row(row, padded);
        }
    }
}

/* Exactly 10 characters per room, two rooms per row:
 *   "R1 100% PS"   lamp level, P = people detected, S = sunlight enough
 *   "R1 offline"   node stopped reporting */
#define ROOM_CELL_W 10

static void room_cell(char *out, size_t len, int room, const ui_status_t *st)
{
    if (!st->rooms[room].online) {
        snprintf(out, len, "R%d offline", room + 1);
    } else {
        snprintf(out, len, "R%d %3u%% %c%c", room + 1, st->rooms[room].level,
                 st->rooms[room].occupied ? 'P' : '-', st->rooms[room].daylight ? 'S' : '-');
    }
}

/* Every row is formatted to exactly LCD_COLS (20) characters:
 *   "CoCIS 09:15:45  OPEN"
 *   "Hours 07:00-22:00"
 *   "In:3   Rdr:OUT ADMIN"
 *   "R1 100% P-R2   0% -S" */
static void redraw(void)
{
    ui_status_t st = { 0 };
    s_provider(&st);

    char row[48], clock_txt[12], cells[2][16];
    clock_format(clock_txt, sizeof(clock_txt), "%H:%M:%S");
    const char *state = st.emergency ? "EMRG" : st.lockdown ? "LOCK" : st.open ? "OPEN" : "SHUT";
    snprintf(row, sizeof(row), "CoCIS %s  %s", clock_txt, state);
    draw_row(0, row);

    if (time_reached_ms(clock_ms(), s_msg_until)) {
        snprintf(row, sizeof(row), "Hours %s", st.hours);
        draw_row(1, row);
        snprintf(row, sizeof(row), "In:%-3u Rdr:%-3s %s", (unsigned)st.inside,
                 side_str(st.reader), st.admin ? "ADMIN" : "");
        draw_row(2, row);
    } else {
        draw_row(1, s_msg[0]);
        draw_row(2, s_msg[1]);
    }

    room_cell(cells[0], sizeof(cells[0]), 0, &st);
    room_cell(cells[1], sizeof(cells[1]), 1, &st);
    snprintf(row, sizeof(row), "%-*s%s", ROOM_CELL_W, cells[0], cells[1]);
    draw_row(3, row);
}

/* ---------------- Sounds (block only this task) ---------------- */
static void play(ui_sound_t s)
{
    switch (s) {
    case UI_SOUND_GRANTED:
        gpio_set_level(PIN_LED_GRANTED, 1);
        s_led_until = clock_ms() + LED_ON_MS;
        buzzer_tone(2000, 120);
        break;
    case UI_SOUND_DENIED:
        for (int i = 0; i < 3; i++) {
            buzzer_tone(400, 120);
            vTaskDelay(pdMS_TO_TICKS(80));
        }
        break;
    case UI_SOUND_KEY:
        buzzer_tone(1500, 30);
        break;
    case UI_SOUND_ALARM:
        for (int i = 0; i < 4; i++) {
            buzzer_tone(900, 150);
            buzzer_tone(1400, 150);
        }
        break;
    case UI_SOUND_INFO:
        buzzer_tone(1200, 80);
        buzzer_tone(1800, 80);
        break;
    }
}

static void ui_task(void *arg)
{
    ui_cmd_t cmd;
    while (1) {
        if (xQueueReceive(s_queue, &cmd, pdMS_TO_TICKS(REFRESH_MS)) == pdTRUE) {
            if (cmd.kind == CMD_MESSAGE) {
                strcpy(s_msg[0], cmd.msg.l1);
                strcpy(s_msg[1], cmd.msg.l2);
                s_msg_until = clock_ms() + cmd.msg.ms;
            } else {
                play(cmd.sound);
            }
        }
        if (s_led_until && time_reached_ms(clock_ms(), s_led_until)) {
            gpio_set_level(PIN_LED_GRANTED, 0);
            s_led_until = 0;
        }
        redraw();
    }
}

/* ---------------- Public API ---------------- */
void ui_init(ui_status_fn provider)
{
    s_provider = provider;
    s_queue    = xQueueCreate(8, sizeof(ui_cmd_t));

    gpio_config_t led = { .pin_bit_mask = 1ULL << PIN_LED_GRANTED, .mode = GPIO_MODE_OUTPUT };
    ESP_ERROR_CHECK(gpio_config(&led));
    buzzer_setup(PIN_BUZZER);

    s_lcd_ok = lcd_init() == ESP_OK;
    if (!s_lcd_ok) {
        ESP_LOGW(TAG, "LCD not found, running headless");
    }
    s_msg_until = clock_ms();
    xTaskCreate(ui_task, "ui", TASK_UI_STACK, NULL, TASK_UI_PRIO, NULL);
}

void ui_message(const char *line1, const char *line2, uint32_t ms)
{
    ui_cmd_t cmd = { .kind = CMD_MESSAGE, .msg.ms = ms };
    snprintf(cmd.msg.l1, sizeof(cmd.msg.l1), "%s", line1 ? line1 : "");
    snprintf(cmd.msg.l2, sizeof(cmd.msg.l2), "%s", line2 ? line2 : "");
    xQueueSend(s_queue, &cmd, 0);               /* cosmetic: never block the caller */
}

void ui_feedback(ui_sound_t sound)
{
    ui_cmd_t cmd = { .kind = CMD_SOUND, .sound = sound };
    xQueueSend(s_queue, &cmd, 0);
}
