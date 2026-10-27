/* Access-control rules for the entrance doors (pure logic, no I/O).
 *
 * The hub gathers the facts about a card scan (who, which door, are they
 * inside, is the building open...) into an access_request_t, and this module
 * returns the verdict. Keeping the rules in one side-effect-free function
 * makes them unit-testable and easy to explain.
 */
#pragma once

#include "cocis_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Opening hours. days_mask bit n = weekday n is a working day (0 = Sunday,
 * same numbering as struct tm.tm_wday). If close < open the window runs
 * overnight (e.g. 22:00-06:00). */
typedef struct {
    uint16_t open_min;      /* minutes after midnight, 0..1439 */
    uint16_t close_min;
    uint8_t  days_mask;
} access_schedule_t;

#define DAYS_MON_FRI  0x3E
#define DAYS_MON_SAT  0x7E
#define DAYS_ALL      0x7F

/* True if the building is open at that weekday / minute of day. */
bool schedule_is_open(const access_schedule_t *s, int wday, int minute_of_day);

/* Minutes until closing if open now, otherwise -1. */
int schedule_minutes_to_close(const access_schedule_t *s, int wday, int minute_of_day);

/* Sanity check for user-supplied schedules. */
bool schedule_is_valid(const access_schedule_t *s);

/* Everything the decision depends on, collected by the caller. */
typedef struct {
    bool        known;          /* card is in the user registry          */
    bool        enabled;        /* user account is active                */
    role_t      role;
    door_side_t side;           /* which reader the card was held at     */
    bool        inside;         /* occupancy ledger says "in the building" */
    bool        schedule_open;  /* building currently within opening hours */
    bool        escort_active;  /* a security/admin exit opened an escort window */
    bool        lockdown;       /* management lockdown (privileged only) */
} access_request_t;

/* The verdict. Use access_granted() to turn it into a yes/no. */
access_reason_t access_decide(const access_request_t *req);

#ifdef __cplusplus
}
#endif
