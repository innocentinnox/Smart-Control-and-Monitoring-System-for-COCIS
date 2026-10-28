/* Access-control rules for the entrance doors (pure logic, no I/O).
 *
 * The hub gathers the facts about a card scan (who, which door, are they
 * inside, is the building open...) into an access_request_t, and this module
 * returns the verdict. Keeping the rules in one side-effect-free function
 * makes them unit-testable and easy to explain.
 *
 * "Pure" means: no hardware, no clock, no globals, no ESP-IDF. Every input
 * is a parameter, so the same file is compiled for the ESP32 and for the PC
 * host tests (test/host/test_core_logic.c). All functions are therefore safe
 * to call from any task.
 */
#pragma once

#include "cocis_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Opening hours. days_mask bit n = weekday n is a working day (0 = Sunday,
 * same numbering as struct tm.tm_wday). If close < open the window runs
 * overnight (e.g. 22:00-06:00).
 *
 * For an overnight window the day bit that counts is the day the window
 * STARTED on: with Friday set, 22:00 Friday -> 06:00 Saturday is open. */
typedef struct {
    uint16_t open_min;      /* minutes after midnight, 0..1439 */
    /* closing time, minutes after midnight, 0..1439 (exclusive: at close_min the building is shut) */
    uint16_t close_min;
    /* bit 0 = Sunday ... bit 6 = Saturday; 1 = open that day */
    uint8_t  days_mask;
} access_schedule_t;

/* Ready-made day masks (bit 0 = Sunday):
 *   0x3E = 0111110b -> Monday..Friday
 *   0x7E = 1111110b -> Monday..Saturday
 *   0x7F = 1111111b -> every day */
#define DAYS_MON_FRI  0x3E
#define DAYS_MON_SAT  0x7E
#define DAYS_ALL      0x7F

/* True if the building is open at that weekday / minute of day.
 * @param s              the schedule
 * @param wday           day of week, 0 = Sunday .. 6 = Saturday (tm_wday)
 * @param minute_of_day  hour * 60 + minute, 0..1439
 * @return true inside the opening window of a working day */
bool schedule_is_open(const access_schedule_t *s, int wday, int minute_of_day);

/* Minutes until closing if open now, otherwise -1.
 * Used by the console status command to show "closes in N min".
 * @param s              the schedule
 * @param wday           day of week, 0 = Sunday .. 6 = Saturday
 * @param minute_of_day  0..1439
 * @return 1..1439 minutes left in the current window, or -1 if closed */
int schedule_minutes_to_close(const access_schedule_t *s, int wday, int minute_of_day);

/* Sanity check for user-supplied schedules (e.g. from the admin CLI).
 * @return true if both times are 0..1439, open != close (a zero-length
 *         window is refused) and days_mask uses only bits 0..6 */
bool schedule_is_valid(const access_schedule_t *s);

/* Everything the decision depends on, collected by the caller. */
typedef struct {
    bool        known;          /* card is in the user registry          */
    bool        enabled;        /* user account is active                */
    /* the user's role (only meaningful when known) */
    role_t      role;
    door_side_t side;           /* which reader the card was held at     */
    bool        inside;         /* occupancy ledger says "in the building" */
    bool        schedule_open;  /* building currently within opening hours */
    bool        escort_active;  /* a security/admin exit opened an escort window */
    bool        lockdown;       /* management lockdown (privileged only) */
} access_request_t;

/* The verdict. Use access_granted() to turn it into a yes/no.
 * @param req  facts about this card scan (see access_request_t)
 * @return ACCESS_OK / ACCESS_OK_PRIVILEGED / ACCESS_OK_ESCORT when the door
 *         should open, otherwise the reason for the refusal. The full rule
 *         list is documented above access_decide() in access_policy.c. */
access_reason_t access_decide(const access_request_t *req);

#ifdef __cplusplus
}
#endif
