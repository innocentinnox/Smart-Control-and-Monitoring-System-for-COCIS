#include "buttons.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "gpio_isr.h"
#include "task_config.h"

#define MAX_GROUPS   4
#define DEBOUNCE_MS  30
#define LOCKOUT_MS   250            /* ignore repeats of the same button */

typedef struct {
    const gpio_num_t *pins;
    size_t            count;
    buttons_cb_t      on_press;
} group_t;

static group_t       s_groups[MAX_GROUPS];
static size_t        s_group_count;
static QueueHandle_t s_queue;

/* ISR argument packs group and pin index into one pointer-sized value. */
#define PACK(g, i)   ((void *)(uintptr_t)(((g) << 8) | (i)))
#define GROUP_OF(v)  ((v) >> 8)
#define INDEX_OF(v)  ((v) & 0xFF)

static void IRAM_ATTR button_isr(void *arg)
{
    uint32_t v = (uint32_t)(uintptr_t)arg;
    BaseType_t woken = pdFALSE;
    xQueueSendFromISR(s_queue, &v, &woken);
    portYIELD_FROM_ISR(woken);
}

static void buttons_task(void *arg)
{
    uint32_t v;
    while (1) {
        xQueueReceive(s_queue, &v, portMAX_DELAY);
        const group_t *g = &s_groups[GROUP_OF(v)];
        size_t i = INDEX_OF(v);

        vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_MS));
        if (gpio_get_level(g->pins[i]) == 0) {      /* still pressed: a real press */
            g->on_press(i);
            vTaskDelay(pdMS_TO_TICKS(LOCKOUT_MS));
        }
        xQueueReset(s_queue);                       /* discard contact bounce */
    }
}

void buttons_add_group(const gpio_num_t *pins, size_t count, buttons_cb_t on_press)
{
    configASSERT(s_group_count < MAX_GROUPS);
    if (!s_queue) {
        s_queue = xQueueCreate(16, sizeof(uint32_t));
        xTaskCreate(buttons_task, "buttons", TASK_BUTTONS_STACK, NULL, TASK_BUTTONS_PRIO, NULL);
        gpio_isr_service_ensure();
    }

    size_t g = s_group_count++;
    s_groups[g] = (group_t){ pins, count, on_press };

    for (size_t i = 0; i < count; i++) {
        gpio_config_t cfg = {
            .pin_bit_mask = 1ULL << pins[i],
            .mode         = GPIO_MODE_INPUT,
            .pull_up_en   = GPIO_PULLUP_ENABLE,
            .intr_type    = GPIO_INTR_NEGEDGE,
        };
        ESP_ERROR_CHECK(gpio_config(&cfg));
        ESP_ERROR_CHECK(gpio_isr_handler_add(pins[i], button_isr, PACK(g, i)));
    }
}
