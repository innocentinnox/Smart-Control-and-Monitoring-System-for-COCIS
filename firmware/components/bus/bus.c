/* Node bus over FreeRTOS queues (see bus.h).
 *
 * Duplicate / replay protection with sequence numbers:
 *   - every sender has ONE counter, shared by all its destinations, that goes
 *     1, 2, 3 ... for each message it successfully queues;
 *   - every receiver remembers the highest seq it accepted from each sender
 *     and drops anything not strictly higher.
 * The receiver only needs "higher than last time", not "exactly last + 1",
 * so the gaps a receiver sees (because the sender's other messages went to
 * a different node) are fine.
 */
#include "bus.h"

#include "freertos/queue.h"
#include "freertos/semphr.h"

#define INBOX_LEN 16                /* messages each inbox can hold before senders must wait */

/* One queue (inbox) per node, indexed by node_id_t. */
static QueueHandle_t s_inbox[NODE_COUNT];

/* Several tasks send as NODE_HUB (keypad, card buttons, break-glass, CLI).
 * "Take the next seq" and "put it in the queue" must happen as one step,
 * otherwise seq 11 could overtake seq 10 and the receiver would reject 10
 * as stale. A per-sender mutex makes the pair atomic.
 * (A FreeRTOS mutex is a lock: only one task can hold it, others that try
 * to take it sleep until it is given back.) */
static SemaphoreHandle_t s_send_lock[NODE_COUNT];
/* last seq successfully sent by each sender (0 = nothing sent yet) */
static uint32_t          s_next_seq[NODE_COUNT];

/* Receiver side: last accepted seq per (receiver, sender). Only the owning
 * receiver task touches its own row, so no lock is needed. */
static uint32_t s_last_seq[NODE_COUNT][NODE_COUNT];
/* volatile: read by other tasks (status page) while receivers update it */
static volatile uint32_t s_dropped;

void bus_init(void)
{
    for (int n = 0; n < NODE_COUNT; n++) {
        s_inbox[n]     = xQueueCreate(INBOX_LEN, sizeof(bus_msg_t));
        s_send_lock[n] = xSemaphoreCreateMutex();
        /* creation only fails when out of heap: stop at boot rather than crash later */
        configASSERT(s_inbox[n] && s_send_lock[n]);
    }
}

bool bus_send(node_id_t src, node_id_t dst, bus_msg_t *msg, TickType_t wait)
{
    if (src >= NODE_COUNT || dst >= NODE_COUNT) {
        return false;
    }
    /* Held while waiting for room in the inbox too, so other tasks sending
     * as the same src queue up behind us (that is what keeps seq in order). */
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
    /* Keep reading until a fresh message turns up or the inbox runs dry. */
    while (xQueueReceive(s_inbox[self], out, wait) == pdTRUE) {
        /* accept only a valid sender with a seq newer than the last accepted one */
        if (out->src < NODE_COUNT && out->seq > s_last_seq[self][out->src]) {
            s_last_seq[self][out->src] = out->seq;
            return true;
        }
        s_dropped++;                    /* duplicate or replay: ignore, try the next one */
        /* after a reject only look at what is already queued, so a stream of
         * duplicates cannot stretch the caller's timeout */
        wait = 0;
    }
    return false;
}

uint32_t bus_dropped_count(void)
{
    return s_dropped;
}
