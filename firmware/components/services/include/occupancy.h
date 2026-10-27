/* Occupancy ledger: who is inside the building right now.
 *
 * Writers: only the hub task (single writer), after an access decision.
 * Readers: hub, CLI, LCD. A mutex guards the table, and every change is
 * persisted to NVS so a reboot does not "forget" people who are inside.
 */
#pragma once

#include <time.h>
#include "cocis_types.h"
#include "esp_err.h"

#define OCCUPANCY_MAX 64

typedef struct {
    card_uid_t uid;
    time_t     since;           /* when they entered */
} presence_t;

esp_err_t occupancy_init(void);
bool      occupancy_is_inside(card_uid_t uid);
void      occupancy_set(card_uid_t uid, bool inside);
size_t    occupancy_count(void);
size_t    occupancy_snapshot(presence_t *out, size_t max);
