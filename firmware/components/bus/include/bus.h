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
 */
#pragma once

#include "cocis_types.h"
#include "freertos/FreeRTOS.h"

typedef enum {
    MSG_CARD_SCAN,      /* reader  -> HUB  */
    MSG_KEY,            /* keypad  -> HUB  */
    MSG_BREAK_GLASS,    /* button  -> HUB  */
    MSG_ROOM_STATE,     /* ROOMn   -> HUB  */
    MSG_HEARTBEAT,      /* ROOMn   -> HUB  */
    MSG_LIGHT_CMD,      /* HUB     -> ROOMn */
} bus_msg_type_t;

typedef struct {
    node_id_t      src;     /* filled in by bus_send() */
    uint32_t       seq;     /* filled in by bus_send() */
    bus_msg_type_t type;
    union {
        struct { card_uid_t uid; door_side_t side; } scan;
        struct { char key; } key;
        struct {
            bool         occupied;
            uint8_t      level;         /* lamp %          */
            uint16_t     lux;
            light_mode_t mode;
            float        on_wh;
            float        saved_wh;
        } room;
        struct { uint32_t uptime_s; } heartbeat;
        struct { light_mode_t mode; } light;
    };
} bus_msg_t;

void bus_init(void);

/* Returns false if the destination inbox stayed full for `wait` ticks. */
bool bus_send(node_id_t src, node_id_t dst, bus_msg_t *msg, TickType_t wait);

/* Returns false on timeout. Duplicates are discarded internally. */
bool bus_recv(node_id_t self, bus_msg_t *out, TickType_t wait);

/* Number of duplicate/stale messages rejected so far (for the status page). */
uint32_t bus_dropped_count(void);
