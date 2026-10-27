#include "access_policy.h"

#define MINUTES_PER_DAY 1440

bool schedule_is_valid(const access_schedule_t *s)
{
    return s->open_min < MINUTES_PER_DAY && s->close_min < MINUTES_PER_DAY &&
           s->open_min != s->close_min && (s->days_mask & ~DAYS_ALL) == 0;
}

int schedule_minutes_to_close(const access_schedule_t *s, int wday, int minute_of_day)
{
    bool overnight = s->close_min < s->open_min;
    int  opened_on = wday;          /* weekday on which the current window began */
    bool in_window;

    if (!overnight) {
        in_window = minute_of_day >= s->open_min && minute_of_day < s->close_min;
    } else if (minute_of_day >= s->open_min) {
        in_window = true;           /* evening part: window began today */
    } else {
        in_window = minute_of_day < s->close_min;
        opened_on = (wday + 6) % 7; /* early-morning part: window began yesterday */
    }

    if (!in_window || !(s->days_mask & (1u << opened_on))) {
        return -1;
    }
    int left = (int)s->close_min - minute_of_day;
    return left > 0 ? left : left + MINUTES_PER_DAY;
}

bool schedule_is_open(const access_schedule_t *s, int wday, int minute_of_day)
{
    return schedule_minutes_to_close(s, wday, minute_of_day) >= 0;
}

/* Rules, evaluated top to bottom; the first one that matches wins:
 *  1. Unknown or disabled cards are always refused.
 *  2. Anti-passback: you can only enter if you are outside, and only exit if
 *     you are inside. This stops one card being passed back to a friend and
 *     keeps the "who is inside" ledger truthful.
 *  3. Lockdown: only security/admin may pass.
 *  4. Within opening hours everyone may pass.
 *  5. Outside opening hours security/admin may still pass (24/7 roles).
 *  6. Outside hours an ordinary user may only EXIT during an escort window
 *     opened by security. Otherwise they are held inside, as the brief
 *     requires, and the hub raises an alert so security comes to them.
 */
access_reason_t access_decide(const access_request_t *req)
{
    if (!req->known) {
        return ACCESS_UNKNOWN_CARD;
    }
    if (!req->enabled) {
        return ACCESS_DISABLED;
    }
    if ((req->side == SIDE_IN && req->inside) || (req->side == SIDE_OUT && !req->inside)) {
        return ACCESS_ANTI_PASSBACK;
    }

    bool privileged = role_is_privileged(req->role);
    if (req->lockdown) {
        return privileged ? ACCESS_OK_PRIVILEGED : ACCESS_LOCKDOWN;
    }
    if (req->schedule_open) {
        return ACCESS_OK;
    }
    if (privileged) {
        return ACCESS_OK_PRIVILEGED;
    }
    if (req->side == SIDE_OUT && req->escort_active) {
        return ACCESS_OK_ESCORT;
    }
    return ACCESS_OUTSIDE_HOURS;
}
