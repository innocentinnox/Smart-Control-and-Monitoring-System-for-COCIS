/* Entrance UI task: owns the LCD, the buzzer and the green "granted" LED.
 *
 * Design:
 *   - other tasks call ui_message()/ui_feedback(), which only copy a small
 *     ui_cmd_t into a FreeRTOS queue and return at once
 *   - ui_task waits on that queue for at most REFRESH_MS (500 ms); whether a
 *     command arrived or the wait timed out, it then redraws the screen, so
 *     the clock on row 0 updates about twice a second
 *   - the screen content comes from the status provider callback (the hub),
 *     and draw_row() only sends rows whose text actually changed
 * Since only this task touches the LCD, buzzer and LED, none of them need a
 * lock.
 */
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

/* Max time between screen redraws (ms) */
#define REFRESH_MS     500
/* How long the green LED stays on after UI_SOUND_GRANTED (ms) */
#define LED_ON_MS      2000

/* What a queued request asks for */
typedef enum { CMD_MESSAGE, CMD_SOUND } cmd_kind_t;

/* One queued request. The text is copied *into* the command (not just a
 * pointer), so the caller's buffers may go away right after ui_message()
 * returns. The union keeps only the fields for the request's kind. */
typedef struct {
    cmd_kind_t kind;
    union {
        struct { char l1[LCD_COLS + 1]; char l2[LCD_COLS + 1]; uint32_t ms; } msg;
        ui_sound_t sound;
    };
} ui_cmd_t;

static QueueHandle_t s_queue;
static ui_status_fn  s_provider;
/* false = LCD not found at boot: keep running, just skip the I2C writes */
static bool          s_lcd_ok;

/* ui-task-only state */
static char     s_shown[LCD_ROWS][LCD_COLS + 1];    /* what is on the glass now */
/* Current temporary message (rows 1 and 2) */
static char     s_msg[2][LCD_COLS + 1];
/* clock_ms() deadlines: when the message expires / when to turn the LED off
 * (s_led_until = 0 means the LED is already off) */
static uint32_t s_msg_until, s_led_until;

/* ---------------- Drawing ---------------- */

/* Only send a row over I2C when its text changed: no flicker, less bus use.
 * @param row   LCD row 0..3
 * @param text  wanted text; padded with spaces / cut to exactly 20 chars so
 *              the comparison with s_shown[] is exact
 * s_shown is updated even when there is no LCD, which is harmless. */
static void draw_row(int row, const char *text)
{
    char padded[LCD_COLS + 1];
    /* "%-20.20s" = left-aligned, at least and at most 20 characters */
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

/* Format one room's 10-character cell (see above) into out.
 * @param out   destination buffer
 * @param len   size of out in bytes (at least 11)
 * @param room  room index, 0-based (shown as R1, R2)
 * @param st    status snapshot to read from */
static void room_cell(char *out, size_t len, int room, const ui_status_t *st)
{
    if (!st->rooms[room].online) {
        snprintf(out, len, "R%d offline", room + 1);
    } else {
        /* %3u pads the level to 3 digits so the cell width stays fixed;
         * '-' means "no" for each flag. */
        snprintf(out, len, "R%d %3u%% %c%c", room + 1, st->rooms[room].level,
                 st->rooms[room].occupied ? 'P' : '-', st->rooms[room].daylight ? 'S' : '-');
    }
}

/* Every row is formatted to exactly LCD_COLS (20) characters:
 *   "CoCIS 09:15:45  OPEN"
 *   "Hours 07:00-22:00"
 *   "Inside: 3      ADMIN"
 *   "R1 100% P-R2   0% -S"
 * Builds all four rows from a fresh status snapshot; draw_row() then sends
 * only the rows that differ from what is already displayed. */
static void redraw(void)
{
    ui_status_t st = { 0 };
    s_provider(&st);

    /* row[] is larger than 20 on purpose: snprintf may build a longer
     * string and draw_row() cuts it to 20. */
    char row[48], clock_txt[12], cells[2][16];
    clock_format(clock_txt, sizeof(clock_txt), "%H:%M:%S");
    /* Most important state wins: emergency > lockdown > open/shut */
    const char *state = st.emergency ? "EMRG" : st.lockdown ? "LOCK" : st.open ? "OPEN" : "SHUT";
    snprintf(row, sizeof(row), "CoCIS %s  %s", clock_txt, state);
    draw_row(0, row);

    /* Rows 1-2: the normal idle info once the message time has passed,
     * otherwise the temporary message. time_reached_ms() compares the
     * deadline in a way that survives the ms counter wrapping around. */
    if (time_reached_ms(clock_ms(), s_msg_until)) {
        snprintf(row, sizeof(row), "Hours %s", st.hours);
        draw_row(1, row);
        snprintf(row, sizeof(row), "Inside: %-3u    %s", (unsigned)st.inside,
                 st.admin ? "ADMIN" : "");
        draw_row(2, row);
    } else {
        draw_row(1, s_msg[0]);
        draw_row(2, s_msg[1]);
    }

    /* Row 3: two 10-character room cells side by side ("%-*s" pads the
     * first cell to ROOM_CELL_W so the second always starts at column 10) */
    room_cell(cells[0], sizeof(cells[0]), 0, &st);
    room_cell(cells[1], sizeof(cells[1]), 1, &st);
    snprintf(row, sizeof(row), "%-*s%s", ROOM_CELL_W, cells[0], cells[1]);
    draw_row(3, row);
}

/* ---------------- Sounds (block only this task) ---------------- */
/* Play one sound pattern. Each buzzer_tone(freq Hz, length ms) sleeps this
 * task for its length, so the longest pattern (ALARM, about 1.2 s) delays
 * the next redraw by that much; other tasks are not affected.
 * GRANTED also turns the LED on; ui_task turns it off after LED_ON_MS. */
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
    case UI_SOUND_ALARM:
        /* Alternating two-tone siren, 4 cycles */
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

/* The ui task's main loop: handle at most one request, switch the LED off
 * when its time is up, redraw. */
static void ui_task(void *arg)
{
    ui_cmd_t cmd;
    while (1) {
        /* Wait for a request, but no longer than REFRESH_MS, so the screen
         * (clock) keeps updating even when nobody sends anything. */
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
    /* Up to 8 pending requests; ui_message()/ui_feedback() drop new ones
     * if it is full. */
    s_queue    = xQueueCreate(8, sizeof(ui_cmd_t));

    gpio_config_t led = { .pin_bit_mask = 1ULL << PIN_LED_GRANTED, .mode = GPIO_MODE_OUTPUT };
    ESP_ERROR_CHECK(gpio_config(&led));
    buzzer_setup(PIN_BUZZER);

    s_lcd_ok = lcd_init() == ESP_OK;
    if (!s_lcd_ok) {
        ESP_LOGW(TAG, "LCD not found, running headless");
    }
    /* "Message expires now" = start on the idle screen */
    s_msg_until = clock_ms();
    xTaskCreate(ui_task, "ui", TASK_UI_STACK, NULL, TASK_UI_PRIO, NULL);
}

void ui_message(const char *line1, const char *line2, uint32_t ms)
{
    /* snprintf both copies and cuts the text to 20 chars + NUL */
    ui_cmd_t cmd = { .kind = CMD_MESSAGE, .msg.ms = ms };
    snprintf(cmd.msg.l1, sizeof(cmd.msg.l1), "%s", line1 ? line1 : "");
    snprintf(cmd.msg.l2, sizeof(cmd.msg.l2), "%s", line2 ? line2 : "");
    xQueueSend(s_queue, &cmd, 0);               /* cosmetic: never block the caller */
}

void ui_feedback(ui_sound_t sound)
{
    /* Timeout 0: if the queue is full the sound is skipped, never waited for */
    ui_cmd_t cmd = { .kind = CMD_SOUND, .sound = sound };
    xQueueSend(s_queue, &cmd, 0);
}
