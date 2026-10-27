#include "auth.h"

#include "clock.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"
#include "storage.h"
#include "users.h"

static const auth_cfg_t CFG = {
    .step_timeout_ms = 30 * 1000,
    .session_ms      = CONFIG_COCIS_ADMIN_SESSION_S * 1000,
    .max_failures    = 3,
    .lockout_ms      = 60 * 1000,
};

static auth_fsm_t        s_fsm;
static SemaphoreHandle_t s_lock;

#define LOCK()   xSemaphoreTake(s_lock, portMAX_DELAY)
#define UNLOCK() xSemaphoreGive(s_lock)

/* One place that turns a state-machine outcome into audit records. */
static auth_result_t audit(auth_result_t r, card_uid_t uid)
{
    if (r != AUTH_NONE) {
        cocis_log(LOG_AUTH, "uid=%08lX result=%s", (unsigned long)uid, auth_result_str(r));
    }
    if (r == AUTH_LOCKED) {
        cocis_log(LOG_ALERT, "code=PIN_LOCKOUT detail=%08lX", (unsigned long)uid);
    }
    return r;
}

void auth_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    auth_fsm_init(&s_fsm);
}

auth_result_t auth_begin(void)
{
    LOCK();
    auth_result_t r = auth_fsm_arm(&s_fsm, &CFG, clock_ms());
    UNLOCK();
    return audit(r, CARD_UID_NONE);
}

auth_result_t auth_card(card_uid_t uid)
{
    user_t u;
    bool admin = users_find(uid, &u) && u.enabled && u.role == ROLE_ADMIN && u.has_pin;
    LOCK();
    auth_result_t r = auth_fsm_card(&s_fsm, &CFG, uid, admin, clock_ms());
    UNLOCK();
    return audit(r, uid);
}

auth_result_t auth_pin(const char *pin)
{
    LOCK();
    card_uid_t uid = s_fsm.uid;
    bool ok = s_fsm.state == AUTH_ST_WAIT_PIN && users_verify_pin(uid, pin);
    auth_result_t r = auth_fsm_pin(&s_fsm, &CFG, ok, clock_ms());
    UNLOCK();
    return audit(r, uid);
}

auth_result_t auth_logout(void)
{
    LOCK();
    card_uid_t uid = s_fsm.uid;
    auth_result_t r = auth_fsm_logout(&s_fsm);
    UNLOCK();
    return audit(r, uid);
}

void auth_tick(void)
{
    LOCK();
    card_uid_t uid = s_fsm.uid;
    auth_result_t r = auth_fsm_tick(&s_fsm, clock_ms());
    UNLOCK();
    audit(r, uid);
}

auth_state_t auth_state(void)
{
    LOCK();
    auth_state_t st = s_fsm.state;
    UNLOCK();
    return st;
}

bool auth_is_admin(void)
{
    LOCK();
    bool ok = auth_fsm_in_session(&s_fsm, clock_ms());
    UNLOCK();
    return ok;
}

card_uid_t auth_session_uid(void)
{
    LOCK();
    card_uid_t uid = auth_fsm_in_session(&s_fsm, clock_ms()) ? s_fsm.uid : CARD_UID_NONE;
    UNLOCK();
    return uid;
}

uint32_t auth_session_left_s(void)
{
    LOCK();
    uint32_t now = clock_ms();
    uint32_t left = auth_fsm_in_session(&s_fsm, now) ? (s_fsm.deadline_ms - now) / 1000 : 0;
    UNLOCK();
    return left;
}
