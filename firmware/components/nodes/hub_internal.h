/* Private interface between the hub's source files (hub.c, hub_access.c,
 * hub_keypad.c). Everything here runs in the hub task only. */
#pragma once

#include "bus.h"
#include "hub.h"

void hub_access_handle_scan(card_uid_t uid, door_side_t side);
void hub_access_tick(void);

void hub_keypad_handle(char key);
void hub_keypad_reset(void);

/* Counters and alerts live in hub.c */
void hub_count_access(bool granted);
void hub_alert(const char *code, const char *detail);
