/* Interrupt-driven 4x4 matrix keypad.
 *
 * Idle: all rows driven LOW, columns are pull-up inputs, so any key press
 * pulls a column LOW and raises a GPIO interrupt. The ISR only wakes the
 * keypad task (task notification); the task debounces, scans the rows to
 * find the key, waits for release and re-arms the interrupts.
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
#define POLL_MS      10
#define DEBOUNCE_MS  20

static const gpio_num_t s_rows[ROWS] = { PIN_KEYPAD_R1, PIN_KEYPAD_R2, PIN_KEYPAD_R3, PIN_KEYPAD_R4 };
static const gpio_num_t s_cols[COLS] = { PIN_KEYPAD_C1, PIN_KEYPAD_C2, PIN_KEYPAD_C3, PIN_KEYPAD_C4 };
static const char s_keymap[ROWS][COLS] = {
    { '1', '2', '3', 'A' },
    { '4', '5', '6', 'B' },
    { '7', '8', '9', 'C' },
    { '*', '0', '#', 'D' },
};

static TaskHandle_t s_task;
static keypad_cb_t  s_on_key;

static void rows_set_all(int level)
{
    for (int r = 0; r < ROWS; r++) {
        gpio_set_level(s_rows[r], level);
    }
}

static void cols_irq_enable(bool enable)
{
    for (int c = 0; c < COLS; c++) {
        enable ? gpio_intr_enable(s_cols[c]) : gpio_intr_disable(s_cols[c]);
    }
}

static bool any_col_low(void)
{
    for (int c = 0; c < COLS; c++) {
        if (gpio_get_level(s_cols[c]) == 0) {
            return true;
        }
    }
    return false;
}

static void IRAM_ATTR keypad_isr(void *arg)
{
    BaseType_t woken = pdFALSE;
    vTaskNotifyGiveFromISR(s_task, &woken);
    portYIELD_FROM_ISR(woken);
}

/* Drive one row low at a time and see which column follows it. */
static char scan(void)
{
    char key = 0;
    rows_set_all(1);
    for (int r = 0; r < ROWS && !key; r++) {
        gpio_set_level(s_rows[r], 0);
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
    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        cols_irq_enable(false);
        vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_MS));

        char key = scan();
        if (key && s_on_key) {
            s_on_key(key);
        }
        while (any_col_low()) {                     /* one press = one event */
            vTaskDelay(pdMS_TO_TICKS(POLL_MS));
        }
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
        .mode = GPIO_MODE_OUTPUT_OD,                /* never short two rows together */
    };
    ESP_ERROR_CHECK(gpio_config(&rows));
    rows_set_all(0);

    gpio_config_t cols = {
        .pin_bit_mask = (1ULL << PIN_KEYPAD_C1) | (1ULL << PIN_KEYPAD_C2) |
                        (1ULL << PIN_KEYPAD_C3) | (1ULL << PIN_KEYPAD_C4),
        .mode       = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .intr_type  = GPIO_INTR_NEGEDGE,
    };
    ESP_ERROR_CHECK(gpio_config(&cols));

    xTaskCreate(keypad_task, "keypad", TASK_KEYPAD_STACK, NULL, TASK_KEYPAD_PRIO, &s_task);
    gpio_isr_service_ensure();
    for (int c = 0; c < COLS; c++) {
        ESP_ERROR_CHECK(gpio_isr_handler_add(s_cols[c], keypad_isr, NULL));
    }
}
