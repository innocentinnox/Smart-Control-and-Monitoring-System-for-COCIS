/* Entrance hub: the single authority of the system.
 *
 * One task consumes the hub inbox (card scans, keypad, break-glass, room
 * reports) and is the only writer of the occupancy ledger and the room
 * table. Serialising every decision through one task is the main answer to
 * the race-condition requirement: two scans can never be decided at once.
 */
#pragma once

#include "cocis_types.h"

typedef struct {
    bool         online;
    bool         occupied;
    uint8_t      level;
    uint16_t     lux;
    light_mode_t mode;
    float        on_wh;
    float        saved_wh;
    uint32_t     last_heard_ms;
} room_status_t;

typedef struct {
    uint32_t granted;
    uint32_t denied;
    uint32_t alerts;
} hub_stats_t;

void hub_start(void);

bool hub_room_status(int room, room_status_t *out);     /* room = 1..ROOM_COUNT */
void hub_stats(hub_stats_t *out);
bool hub_emergency_active(void);
void hub_emergency_clear(card_uid_t by);
bool hub_escort_active(void);
