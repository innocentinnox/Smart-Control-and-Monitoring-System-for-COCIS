/* Interrupt-driven 4x4 matrix keypad.
 *
 * Idle: all rows driven LOW, columns are pull-up inputs, so any key press
 * pulls a column LOW and raises a GPIO interrupt. The ISR only wakes the
 * keypad task (task notification); the task debounces, scans the rows to
 * find the key, waits for release and re-arms the interrupts.
 *
 * The idle state only tells us *which column* went LOW, not which row, so
 * the task then "scans": it releases all rows and pulls them LOW one at a
 * time. The column only reads LOW while the row of the pressed key is the
 * one being pulled, which gives row + column = the key.
 *
 * A "task notification" is FreeRTOS's lightest way to wake one specific
 * task: a counter built into the task that the ISR increments and the task
 * waits on (no separate queue object needed).
 */
#include "keypad.h"

#include "board_pins.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gpio_isr.h"
#include "task_config.h"

#define ROWS 4
#define COLS 4
/* How often to check for key release while a key is held (ms) */
#define POLL_MS      10
/* Settle time after a press and after a release before reading pins (ms) */
#define DEBOUNCE_MS  20
#define REPEAT_MS    120            /* same key again this soon = a glitch, not a press */

static const gpio_num_t s_rows[ROWS] = { PIN_KEYPAD_R1, PIN_KEYPAD_R2, PIN_KEYPAD_R3, PIN_KEYPAD_R4 };
static const gpio_num_t s_cols[COLS] = { PIN_KEYPAD_C1, PIN_KEYPAD_C2, PIN_KEYPAD_C3, PIN_KEYPAD_C4 };
/* Character printed on the key at [row][column] */
static const char s_keymap[ROWS][COLS] = {
    { '1', '2', '3', 'A' },
    { '4', '5', '6', 'B' },
    { '7', '8', '9', 'C' },
    { '*', '0', '#', 'D' },
};

/* Handle of the keypad task, so the ISR knows which task to notify */
static TaskHandle_t s_task;
static keypad_cb_t  s_on_key;

/* Set every row pin to level (0 = pull LOW, 1 = release; rows are open-drain
 * so "1" means floating and the column pull-ups win). */
static void rows_set_all(int level)
{
    for (int r = 0; r < ROWS; r++) {
        gpio_set_level(s_rows[r], level);
    }
}

/* Turn the column interrupts on/off. They are off while the task scans,
 * because scanning itself toggles the columns and would re-trigger the ISR. */
static void cols_irq_enable(bool enable)
{
    for (int c = 0; c < COLS; c++) {
        enable ? gpio_intr_enable(s_cols[c]) : gpio_intr_disable(s_cols[c]);
    }
}

/* True while any key is still held (rows are all LOW in idle state, so a
 * held key keeps its column LOW). */
static bool any_col_low(void)
{
    for (int c = 0; c < COLS; c++) {
        if (gpio_get_level(s_cols[c]) == 0) {
            return true;
        }
    }
    return false;
}

/* Column falling-edge ISR: just wake the keypad task and return. All the
 * slow work (delays, scanning, callback) happens in the task. IRAM_ATTR
 * keeps the handler in internal RAM, which ESP32 ISRs need. */
static void IRAM_ATTR keypad_isr(void *arg)
{
    BaseType_t woken = pdFALSE;
    vTaskNotifyGiveFromISR(s_task, &woken);
    portYIELD_FROM_ISR(woken);
}

/* Drive one row low at a time and see which column follows it.
 * Returns the key character, or 0 if nothing is pressed (e.g. it was only
 * a bounce). Leaves all rows LOW again (idle state) when done. */
static char scan(void)
{
    char key = 0;
    rows_set_all(1);
    for (int r = 0; r < ROWS && !key; r++) {
        gpio_set_level(s_rows[r], 0);
        /* Give the line a moment to settle before reading the columns */
        esp_rom_delay_us(10);
        for (int c = 0; c < COLS; c++) {
            if (gpio_get_level(s_cols[c]) == 0) {
                key = s_keymap[r][c];
                break;
            }
        }
        gpio_set_level(s_rows[r], 1);
    }
    rows_set_all(0);
    return key;
}

static void keypad_task(void *arg)
{
    /* Last key reported and when it was released, for the repeat guard */
    char       last_key = 0;
    TickType_t last_release = 0;

    while (1) {
        /* Sleep until the ISR notifies us (pdTRUE = clear the count) */
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        cols_irq_enable(false);
        vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_MS));

        char key = scan();
        /* Repeat guard: the same key reported again within REPEAT_MS of its
         * release is almost certainly release bounce, not a second press.
         * A different key, or the same key after a real pause, is accepted.
         * (Tick counts are unsigned, so the subtraction is wrap-safe.) */
        bool repeat = key == last_key && xTaskGetTickCount() - last_release < pdMS_TO_TICKS(REPEAT_MS);
        if (key && !repeat && s_on_key) {
            s_on_key(key);
        }
        /* Holding a key must not auto-repeat: wait here until released */
        while (any_col_low()) {                     /* one press = one event */
            vTaskDelay(pdMS_TO_TICKS(POLL_MS));
        }
        if (key) {
            last_key     = key;
            last_release = xTaskGetTickCount();
        }
        /* Let the release bounce die out, throw away any notifications it
         * caused while we were busy, then listen for the next press. */
        vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_MS));
        ulTaskNotifyTake(pdTRUE, 0);                /* drop bounce notifications */
        cols_irq_enable(true);
    }
}

void keypad_init(keypad_cb_t on_key)
{
    s_on_key = on_key;

    gpio_config_t rows = {
        .pin_bit_mask = (1ULL << PIN_KEYPAD_R1) | (1ULL << PIN_KEYPAD_R2) |
                        (1ULL << PIN_KEYPAD_R3) | (1ULL << PIN_KEYPAD_R4),
        /* Open-drain: a row can only pull LOW or float, never push HIGH, so
         * pressing two keys in one column cannot short a HIGH row to a LOW
         * row. */
        .mode = GPIO_MODE_OUTPUT_OD,                /* never short two rows together */
    };
    ESP_ERROR_CHECK(gpio_config(&rows));
    rows_set_all(0);

    /* Columns: inputs with pull-ups, interrupt when one falls (key down) */
    gpio_config_t cols = {
        .pin_bit_mask = (1ULL << PIN_KEYPAD_C1) | (1ULL << PIN_KEYPAD_C2) |
                        (1ULL << PIN_KEYPAD_C3) | (1ULL << PIN_KEYPAD_C4),
        .mode       = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .intr_type  = GPIO_INTR_NEGEDGE,
    };
    ESP_ERROR_CHECK(gpio_config(&cols));

    /* Create the task before adding the ISR handlers, so s_task is valid by
     * the time the first interrupt can arrive. */
    xTaskCreate(keypad_task, "keypad", TASK_KEYPAD_STACK, NULL, TASK_KEYPAD_PRIO, &s_task);
    gpio_isr_service_ensure();
    for (int c = 0; c < COLS; c++) {
        ESP_ERROR_CHECK(gpio_isr_handler_add(s_cols[c], keypad_isr, NULL));
    }
}
