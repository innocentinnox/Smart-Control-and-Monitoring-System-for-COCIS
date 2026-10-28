/* Entrance hub: the single authority of the system.
 *
 * One task consumes the hub inbox (card scans, keypad, break-glass, room
 * reports) and is the only writer of the occupancy ledger and the room
 * table. Serialising every decision through one task is the main answer to
 * the race-condition requirement: two scans can never be decided at once.
 *
 * Other tasks (CLI, ui) only read the hub's state, through the query
 * functions below, which copy the data out under a mutex.
 *
 * Source files:
 *   hub.c         the hub task, message dispatch, room table, node health,
 *                 break-glass, periodic duties (energy log)
 *   hub_access.c  card scan -> access decision -> door / ledger / log / LCD
 *   hub_keypad.c  keypad -> admin login (card + PIN)
 */
#pragma once

#include "cocis_types.h"

/* The hub's latest picture of one lecture room, filled from the room's
 * MSG_ROOM_STATE reports. */
typedef struct {
    bool         online;        /* room has reported and not been silent too long */
    bool         occupied;      /* PIR saw someone within the grace period        */
    bool         daylight;      /* room is bright enough without the lamp         */
    uint8_t      level;         /* lamp level, 0..100 %                           */
    uint16_t     lux;           /* last light reading, lux                        */
    light_mode_t mode;          /* AUTO, or a manual ON/OFF override              */
    float        on_wh;         /* energy actually used since boot, Wh            */
    float        saved_wh;      /* energy saved vs. "lamp always on in hours", Wh */
    uint32_t     last_heard_ms; /* clock_ms() of the last report or heartbeat     */
} room_status_t;

/* Access counters since boot (shown by the CLI status page). */
typedef struct {
    uint32_t granted;           /* card scans that opened a door        */
    uint32_t denied;            /* card scans that were refused         */
    uint32_t alerts;            /* ALERT events raised (any kind)       */
} hub_stats_t;

/* Initialise the hub's devices (doors, LCD/ui, RFID buttons, keypad,
 * break-glass button) and start the hub task.
 * Call once at boot, after storage, users, config, occupancy, auth and the
 * bus are initialised (see main.c), because the hub uses all of them. */
void hub_start(void);

/* Copy the hub's view of one room.
 * @param room  room number, 1..ROOM_COUNT
 * @param out   receives a copy of the room's status
 * @return false if `room` is out of range (out is left untouched).
 * Safe from any task. */
bool hub_room_status(int room, room_status_t *out);     /* room = 1..ROOM_COUNT */

/* Copy the access/alert counters into *out. Safe from any task. */
void hub_stats(hub_stats_t *out);

/* @return true while the break-glass emergency is active (doors held open). */
bool hub_emergency_active(void);

/* End the break-glass emergency: release and close both doors and log who
 * cleared it. Does nothing if no emergency is active.
 * @param by  card UID of the admin who cleared it (for the audit log)
 * Called by the CLI (admin session required there). */
void hub_emergency_clear(card_uid_t by);

/* @return true while an after-hours escort window is open, i.e. a security
 * or admin card recently left through the exit after hours, so ordinary
 * users may also exit for CONFIG_COCIS_ESCORT_S seconds. */
bool hub_escort_active(void);
