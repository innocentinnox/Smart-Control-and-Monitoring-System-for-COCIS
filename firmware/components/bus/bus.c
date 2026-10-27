#include "bus.h"

#include "freertos/queue.h"
#include "freertos/semphr.h"

#define INBOX_LEN 16

static QueueHandle_t s_inbox[NODE_COUNT];

/* Several tasks send as NODE_HUB (keypad, card buttons, break-glass, CLI).
 * "Take the next seq" and "put it in the queue" must happen as one step,
 * otherwise seq 11 could overtake seq 10 and the receiver would reject 10
 * as stale. A per-sender mutex makes the pair atomic. */
static SemaphoreHandle_t s_send_lock[NODE_COUNT];
static uint32_t          s_next_seq[NODE_COUNT];

/* Receiver side: last accepted seq per (receiver, sender). Only the owning
 * receiver task touches its own row, so no lock is needed. */
static uint32_t s_last_seq[NODE_COUNT][NODE_COUNT];
static volatile uint32_t s_dropped;

void bus_init(void)
{
    for (int n = 0; n < NODE_COUNT; n++) {
        s_inbox[n]     = xQueueCreate(INBOX_LEN, sizeof(bus_msg_t));
        s_send_lock[n] = xSemaphoreCreateMutex();
        configASSERT(s_inbox[n] && s_send_lock[n]);
    }
}

bool bus_send(node_id_t src, node_id_t dst, bus_msg_t *msg, TickType_t wait)
{
    if (src >= NODE_COUNT || dst >= NODE_COUNT) {
        return false;
    }
    xSemaphoreTake(s_send_lock[src], portMAX_DELAY);
    msg->src = src;
    msg->seq = s_next_seq[src] + 1;
    bool sent = xQueueSend(s_inbox[dst], msg, wait) == pdTRUE;
    if (sent) {
        s_next_seq[src] = msg->seq;     /* a dropped frame does not burn a number */
    }
    xSemaphoreGive(s_send_lock[src]);
    return sent;
}

bool bus_recv(node_id_t self, bus_msg_t *out, TickType_t wait)
{
    while (xQueueReceive(s_inbox[self], out, wait) == pdTRUE) {
        if (out->src < NODE_COUNT && out->seq > s_last_seq[self][out->src]) {
            s_last_seq[self][out->src] = out->seq;
            return true;
        }
        s_dropped++;                    /* duplicate or replay: ignore, try the next one */
        wait = 0;
    }
    return false;
}

uint32_t bus_dropped_count(void)
{
    return s_dropped;
}
