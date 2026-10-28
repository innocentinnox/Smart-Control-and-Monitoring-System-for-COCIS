/* Debounced push-button driver.
 *
 * Flow of one press:
 *   1. the pin falls HIGH -> LOW, the GPIO interrupt fires button_isr()
 *   2. the ISR only puts "which button" into a FreeRTOS queue (an ISR must
 *      be very short and must not block)
 *   3. buttons_task wakes up, waits DEBOUNCE_MS, and if the pin is still
 *      LOW treats it as a real press and calls the group's callback
 *   4. a LOCKOUT_MS pause and a queue flush swallow the contact bounce
 *
 * A FreeRTOS queue is a thread-safe FIFO: one side sends items, the other
 * side receives them, and a receiving task sleeps until an item arrives.
 */
#include "buttons.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "gpio_isr.h"
#include "task_config.h"

/* Maximum number of buttons_add_group() calls */
#define MAX_GROUPS   4
/* How long the pin must stay LOW after the edge to count as a press (ms) */
#define DEBOUNCE_MS  30
#define LOCKOUT_MS   250            /* ignore repeats of the same button */

/* One registered group of buttons */
typedef struct {
    const gpio_num_t *pins;         /* caller's pin array (not copied)       */
    size_t            count;        /* number of pins in the array           */
    buttons_cb_t      on_press;     /* called with the index of the press    */
} group_t;

static group_t       s_groups[MAX_GROUPS];
static size_t        s_group_count;
/* Queue of packed (group, index) values from the ISR to the task */
static QueueHandle_t s_queue;

/* ISR argument packs group and pin index into one pointer-sized value.
 * Bits 8 and up = group number, low 8 bits = pin index inside the group.
 * This avoids allocating a separate argument struct for every pin. */
#define PACK(g, i)   ((void *)(uintptr_t)(((g) << 8) | (i)))
#define GROUP_OF(v)  ((v) >> 8)
#define INDEX_OF(v)  ((v) & 0xFF)

/* Interrupt handler shared by every button pin; arg is the PACK()ed value.
 * IRAM_ATTR places the code in internal RAM so it can run even while the
 * flash cache is busy (required for ISRs on the ESP32). "FromISR" queue
 * calls never block; if the send woke a higher-priority task, "woken" is
 * set and portYIELD_FROM_ISR switches to it as soon as the ISR returns. */
static void IRAM_ATTR button_isr(void *arg)
{
    uint32_t v = (uint32_t)(uintptr_t)arg;
    BaseType_t woken = pdFALSE;
    xQueueSendFromISR(s_queue, &v, &woken);
    portYIELD_FROM_ISR(woken);
}

/* The one task that debounces all buttons and runs the callbacks, so the
 * callbacks run in normal task context and never inside the ISR. */
static void buttons_task(void *arg)
{
    uint32_t v;
    while (1) {
        /* Sleep until the ISR reports an edge (portMAX_DELAY = wait forever) */
        xQueueReceive(s_queue, &v, portMAX_DELAY);
        const group_t *g = &s_groups[GROUP_OF(v)];
        size_t i = INDEX_OF(v);

        /* Let the contact settle, then look at the pin again. A bounce or
         * noise spike will have gone back HIGH by now. */
        vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_MS));
        if (gpio_get_level(g->pins[i]) == 0) {      /* still pressed: a real press */
            g->on_press(i);
            vTaskDelay(pdMS_TO_TICKS(LOCKOUT_MS));
        }
        /* Every bounce edge queued another item; throw them all away so one
         * press gives one callback. Note this also drops any other button
         * that was pressed during the debounce/lockout time. */
        xQueueReset(s_queue);                       /* discard contact bounce */
    }
}

void buttons_add_group(const gpio_num_t *pins, size_t count, buttons_cb_t on_press)
{
    configASSERT(s_group_count < MAX_GROUPS);
    /* First call only: create the shared queue (16 pending edges) and task,
     * and make sure the GPIO ISR service exists. */
    if (!s_queue) {
        s_queue = xQueueCreate(16, sizeof(uint32_t));
        xTaskCreate(buttons_task, "buttons", TASK_BUTTONS_STACK, NULL, TASK_BUTTONS_PRIO, NULL);
        gpio_isr_service_ensure();
    }

    size_t g = s_group_count++;
    s_groups[g] = (group_t){ pins, count, on_press };

    /* Each pin: input with pull-up, interrupt on the falling edge (press),
     * and the shared ISR with this pin's (group, index) as its argument. */
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
