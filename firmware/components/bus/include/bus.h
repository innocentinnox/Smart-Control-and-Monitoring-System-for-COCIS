/* Node bus: how the three terminals (entrance hub, room 1, room 2) talk.
 *
 * Each node has an inbox. bus_send() stamps the message with the sender id
 * and a per-sender sequence number; bus_recv() drops any message whose seq
 * is not newer than the last one accepted from that sender, so a resent or
 * replayed frame can never be processed twice.
 *
 * Transport: FreeRTOS queues in this single-board build (all terminals run
 * as task groups on one ESP32-S3, as Wokwi simulates one board). On separate
 * boards the same struct goes over ESP-NOW; only bus.c changes.
 *
 * FreeRTOS queue in one sentence: a fixed-size, thread-safe FIFO that COPIES
 * each item in and out, and lets a task sleep (using no CPU) until an item
 * arrives or there is room, for at most a given number of "ticks" (the
 * scheduler's time unit; convert from ms with pdMS_TO_TICKS(ms)).
 * Because messages are copied, the sender may reuse its bus_msg_t at once.
 *
 * Rule of the architecture: the room nodes never talk to each other and
 * never touch hub state; everything goes through the hub's inbox.
 */
#pragma once

#include "cocis_types.h"
#include "freertos/FreeRTOS.h"

/* What a message means; selects which member of the union below is valid. */
typedef enum {
    MSG_CARD_SCAN,      /* reader  -> HUB  */
    MSG_BREAK_GLASS,    /* button  -> HUB  */
    MSG_ROOM_STATE,     /* ROOMn   -> HUB  */
    MSG_HEARTBEAT,      /* ROOMn   -> HUB  */
    MSG_LIGHT_CMD,      /* HUB     -> ROOMn */
} bus_msg_type_t;

/* One bus message. Fixed size (the union is as big as its largest member),
 * which is what a FreeRTOS queue needs. Read only the union member that
 * matches `type`:
 *   MSG_CARD_SCAN  -> scan        MSG_ROOM_STATE -> room
 *   MSG_LIGHT_CMD  -> light       MSG_HEARTBEAT  -> heartbeat
 *   MSG_BREAK_GLASS: no payload */
typedef struct {
    node_id_t      src;     /* filled in by bus_send() */
    uint32_t       seq;     /* filled in by bus_send() */
    /* set by the sender before calling bus_send() */
    bus_msg_type_t type;
    union {
        /* a card held at the entry (SIDE_IN) or exit (SIDE_OUT) reader */
        struct { card_uid_t uid; door_side_t side; } scan;
        /* room snapshot, sent on changes and periodically (see room_logic.h) */
        struct {
            /* someone is in the room */
            bool         occupied;
            bool         daylight;      /* bright enough without the lamp */
            uint8_t      level;         /* lamp %          */
            /* measured light level, lux */
            uint16_t     lux;
            /* current override mode (AUTO / ON / OFF) */
            light_mode_t mode;
            /* energy used since boot, Wh */
            float        on_wh;
            /* energy saved vs. an always-on room since boot, Wh */
            float        saved_wh;
        } room;
        /* "still alive", seconds since boot; lets the hub spot a dead node */
        struct { uint32_t uptime_s; } heartbeat;
        /* hub -> room: new light override mode */
        struct { light_mode_t mode; } light;
    };
} bus_msg_t;

/* Create one inbox queue and one send mutex per node.
 * Call once at boot, before any task uses the bus. Asserts (halts) if
 * there is not enough memory. */
void bus_init(void);

/* Returns false if the destination inbox stayed full for `wait` ticks.
 *
 * Sets msg->src and msg->seq, then copies the message into dst's inbox.
 * @param src   node the message is sent as (several tasks may share one,
 *              e.g. everything on the entrance terminal sends as NODE_HUB)
 * @param dst   receiving node
 * @param msg   message with `type` and the payload filled in; src and seq
 *              are overwritten
 * @param wait  max ticks to wait for room in the inbox; 0 = do not wait
 *              (room nodes use 0 so the sensing loop never stalls)
 * @return true if queued; false if the inbox was full or src/dst invalid
 * Context: any task, not from an ISR (it takes a mutex). */
bool bus_send(node_id_t src, node_id_t dst, bus_msg_t *msg, TickType_t wait);

/* Returns false on timeout. Duplicates are discarded internally.
 *
 * @param self  the calling node's id: only that node's task may call this
 *              (the per-sender seq table for `self` is not locked)
 * @param out   receives the next new message
 * @param wait  max ticks to wait for the first message; portMAX_DELAY waits
 *              forever. Rejected duplicates do not restart the wait.
 * @return true with *out filled; false if nothing new arrived in time
 *         (*out may then hold a rejected duplicate, ignore it) */
bool bus_recv(node_id_t self, bus_msg_t *out, TickType_t wait);

/* Number of duplicate/stale messages rejected so far (for the status page).
 * Total over all receivers since boot. Call from any task. */
uint32_t bus_dropped_count(void);
