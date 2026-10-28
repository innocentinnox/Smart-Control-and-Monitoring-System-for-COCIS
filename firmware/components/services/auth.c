/* Admin authentication service (see auth.h).
 *
 * Data: one auth_fsm_t (s_fsm) in RAM only. Nothing is saved to NVS, so a
 * reboot always logs the admin out.
 *
 * Pattern of every function: take the mutex, feed the FSM the event plus
 * the current time (clock_ms()), release the mutex, then pass the result
 * through audit() so every outcome ends up in the event log.
 */
#include "auth.h"

#include "clock.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"
#include "storage.h"
#include "users.h"

/* Login timing rules handed to the FSM on every call. */
static const auth_cfg_t CFG = {
    .step_timeout_ms = 30 * 1000,                           /* tap card / type PIN within 30 s */
    .session_ms      = CONFIG_COCIS_ADMIN_SESSION_S * 1000, /* session length (menuconfig) */
    .max_failures    = 3,                                   /* wrong PINs before lockout */
    .lockout_ms      = 60 * 1000,                           /* lockout length */
};

static auth_fsm_t        s_fsm;     /* the single shared login state machine */
static SemaphoreHandle_t s_lock;    /* FreeRTOS mutex: hub and CLI both call in */

#define LOCK()   xSemaphoreTake(s_lock, portMAX_DELAY)
#define UNLOCK() xSemaphoreGive(s_lock)

/* One place that turns a state-machine outcome into audit records.
 * AUTH_NONE ("nothing happened", e.g. a normal door tap while no login is in
 * progress) is not logged. Returns r unchanged so callers can write
 * `return audit(r, uid);`.
 * @param r    FSM result.
 * @param uid  card involved (CARD_UID_NONE if not known yet). */
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
    /* Decide "is this an admin card?" before taking our lock: users_find()
     * has its own mutex and the FSM only needs the yes/no answer. An admin
     * without a PIN could never finish the second factor, so it is refused. */
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
    /* The PIN belongs to the card tapped in the previous step (s_fsm.uid).
     * Only hash it when the FSM is actually waiting for a PIN; otherwise
     * the FSM ignores the event (AUTH_NONE) anyway. */
    card_uid_t uid = s_fsm.uid;
    bool ok = s_fsm.state == AUTH_ST_WAIT_PIN && users_verify_pin(uid, pin);
    auth_result_t r = auth_fsm_pin(&s_fsm, &CFG, ok, clock_ms());
    UNLOCK();
    return audit(r, uid);
}

auth_result_t auth_logout(void)
{
    LOCK();
    /* Remember who it was before the FSM clears uid, for the log line. */
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
    /* auth_fsm_in_session() also checks the deadline, so a session counts
     * as over the moment it expires, even before auth_tick() runs. */
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
    /* In a session, deadline_ms is the session end, so the unsigned
     * difference is the time left (correct across the 32-bit wrap). */
    uint32_t now = clock_ms();
    uint32_t left = auth_fsm_in_session(&s_fsm, now) ? (s_fsm.deadline_ms - now) / 1000 : 0;
    UNLOCK();
    return left;
}
