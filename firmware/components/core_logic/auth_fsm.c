#include "auth_fsm.h"

#include <string.h>

void auth_fsm_init(auth_fsm_t *a)
{
    memset(a, 0, sizeof(*a));
}

static void go_idle(auth_fsm_t *a)
{
    a->state = AUTH_ST_IDLE;
    a->uid   = CARD_UID_NONE;
}

static bool is_locked(auth_fsm_t *a, uint32_t now_ms)
{
    if (a->locked && time_reached_ms(now_ms, a->locked_until_ms)) {
        a->locked   = false;
        a->failures = 0;
    }
    return a->locked;
}

auth_result_t auth_fsm_arm(auth_fsm_t *a, const auth_cfg_t *cfg, uint32_t now_ms)
{
    if (is_locked(a, now_ms)) {
        return AUTH_LOCKED;
    }
    a->state       = AUTH_ST_WAIT_CARD;
    a->uid         = CARD_UID_NONE;
    a->deadline_ms = now_ms + cfg->step_timeout_ms;
    return AUTH_NONE;
}

/* A step answered after its deadline counts as a timeout, even if the
 * periodic tick has not noticed yet. */
static bool step_expired(auth_fsm_t *a, uint32_t now_ms)
{
    if (time_reached_ms(now_ms, a->deadline_ms)) {
        go_idle(a);
        return true;
    }
    return false;
}

auth_result_t auth_fsm_card(auth_fsm_t *a, const auth_cfg_t *cfg, card_uid_t uid,
                            bool is_admin, uint32_t now_ms)
{
    if (a->state != AUTH_ST_WAIT_CARD) {
        return AUTH_NONE;
    }
    if (step_expired(a, now_ms)) {
        return AUTH_TIMEOUT;
    }
    if (!is_admin) {
        go_idle(a);
        return AUTH_NOT_ADMIN;
    }
    a->state       = AUTH_ST_WAIT_PIN;
    a->uid         = uid;
    a->deadline_ms = now_ms + cfg->step_timeout_ms;
    return AUTH_CARD_OK;
}

auth_result_t auth_fsm_pin(auth_fsm_t *a, const auth_cfg_t *cfg, bool pin_ok, uint32_t now_ms)
{
    if (a->state != AUTH_ST_WAIT_PIN) {
        return AUTH_NONE;
    }
    if (step_expired(a, now_ms)) {
        return AUTH_TIMEOUT;
    }
    if (pin_ok) {
        a->state       = AUTH_ST_SESSION;
        a->failures    = 0;
        a->deadline_ms = now_ms + cfg->session_ms;
        return AUTH_OK;
    }
    if (++a->failures >= cfg->max_failures) {
        a->locked          = true;
        a->locked_until_ms = now_ms + cfg->lockout_ms;
        go_idle(a);
        return AUTH_LOCKED;
    }
    return AUTH_BAD_PIN;                    /* stay in WAIT_PIN: allow a retry */
}

auth_result_t auth_fsm_logout(auth_fsm_t *a)
{
    bool had_session = a->state == AUTH_ST_SESSION;
    go_idle(a);
    return had_session ? AUTH_LOGOUT : AUTH_NONE;
}

auth_result_t auth_fsm_tick(auth_fsm_t *a, uint32_t now_ms)
{
    is_locked(a, now_ms);                   /* lets a lockout expire on its own */
    if (a->state != AUTH_ST_IDLE && time_reached_ms(now_ms, a->deadline_ms)) {
        go_idle(a);
        return AUTH_TIMEOUT;
    }
    return AUTH_NONE;
}

bool auth_fsm_in_session(const auth_fsm_t *a, uint32_t now_ms)
{
    return a->state == AUTH_ST_SESSION && !time_reached_ms(now_ms, a->deadline_ms);
}
