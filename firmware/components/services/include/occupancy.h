/* Occupancy ledger: who is inside the building right now.
 *
 * The ledger is what makes anti-passback work: a card may only go IN if it
 * is currently outside and only OUT if it is inside, so one card cannot be
 * passed back through the door to let a second person in. It also gives a
 * muster list ("who") for an evacuation.
 *
 * Writers: only the hub task (single writer), after an access decision.
 * Readers: hub, CLI, LCD. A mutex guards the table, and every change is
 * persisted to NVS so a reboot does not "forget" people who are inside.
 */
#pragma once

#include <time.h>
#include "cocis_types.h"
#include "esp_err.h"

#define OCCUPANCY_MAX 64            /* max people tracked inside at once */

/* One person inside the building. */
typedef struct {
    card_uid_t uid;
    time_t     since;           /* when they entered */
} presence_t;

/* Restore the ledger from NVS (empty if missing or invalid). Call once at
 * boot. @return always ESP_OK (a missing ledger is not an error). */
esp_err_t occupancy_init(void);

/* @return true if the card is currently recorded as inside. Safe from any task. */
bool      occupancy_is_inside(card_uid_t uid);

/* Record that a card went in (inside = true) or out (inside = false).
 * Saves to NVS only if something actually changed. Hub task only.
 * Note: when the ledger is full (OCCUPANCY_MAX) a new entry is silently
 * not recorded.
 *
 * @param uid     card that passed the door.
 * @param inside  true after a granted entry, false after a granted exit.
 */
void      occupancy_set(card_uid_t uid, bool inside);

/* @return number of people currently inside. Safe from any task. */
size_t    occupancy_count(void);

/* Copy up to `max` entries into `out` (for the "who" list). Safe from any task.
 *
 * @param out  caller's array of at least `max` entries.
 * @param max  capacity; OCCUPANCY_MAX copies everything.
 * @return number of entries copied.
 */
size_t    occupancy_snapshot(presence_t *out, size_t max);
