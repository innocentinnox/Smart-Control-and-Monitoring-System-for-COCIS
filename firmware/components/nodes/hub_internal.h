/* Private interface between the hub's source files (hub.c, hub_access.c,
 * hub_keypad.c). Everything here runs in the hub task only, so these
 * functions do not need their own locking (except where hub.c shares data
 * with other tasks; it takes its mutex itself). */
#pragma once

#include "bus.h"
#include "hub.h"

/* Handle one card scan (a MSG_CARD_SCAN from the hub inbox): either the
 * first factor of an admin login, or a door request that is decided by
 * access_decide() and then acted on (door, ledger, log, LCD, buzzer).
 * @param uid   UID of the card that was presented
 * @param side  reader it was presented at: SIDE_IN (entry) or SIDE_OUT (exit) */
void hub_access_handle_scan(card_uid_t uid, door_side_t side);

/* Once-per-second duty of hub_access.c: closes the escort window when its
 * time is up. Called by the hub task's 1 s tick. */
void hub_access_tick(void);

/* Handle one keypad key (a MSG_KEY from the hub inbox): drives the admin
 * login (A = start, digits = PIN, * = clear, # = submit, D = log out).
 * @param key  the key character: '0'-'9', 'A'-'D', '*' or '#' */
void hub_keypad_handle(char key);

/* Forget any half-typed PIN digits. */
void hub_keypad_reset(void);

/* Counters and alerts live in hub.c */

/* Count one access decision in the hub statistics.
 * @param granted  true = door opened, false = refused */
void hub_count_access(bool granted);

/* Count an alert and write it to the event log as an ALERT record.
 * @param code    short alert name, e.g. "NODE_OFFLINE", "BREAK_GLASS"
 * @param detail  extra info, e.g. "ROOM1" or a user name (no spaces) */
void hub_alert(const char *code, const char *detail);
