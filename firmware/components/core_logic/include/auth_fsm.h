/* Two-factor admin login state machine (pure logic, no I/O).
 *
 *   IDLE --arm--> WAIT_CARD --admin card--> WAIT_PIN --correct PIN--> SESSION
 *     ^              |  non-admin card / 30 s     |  wrong PIN x3 -> locked 60 s
 *     +--------------+--------------------------- +-------- session expires / logout
 *
 * Factor 1 = something you have (admin RFID card), factor 2 = something you
 * know (PIN, typed on the serial console). Only during a SESSION
 * may the schedule, users, clock or light overrides be changed.
 *
 * The PIN itself is checked by the caller (users_verify_pin); this module only
 * gets a yes/no, which keeps it free of crypto and storage.
 *
 * Time is passed in as now_ms on every call (the module never reads a clock),
 * so tests can drive it with made-up times. All deadlines are compared with
 * time_reached_ms(), which is safe across the 32-bit ms counter wrap.
 * The timings in the diagram (30 s, x3, 60 s) are the values the caller puts
 * in auth_cfg_t, not constants of this module.
 *
 * Not thread-safe by itself: one auth_fsm_t must be used by one task at a
 * time. The services/auth.c wrapper owns the single instance and guards
 * every call with a mutex, because more than one task can drive a login
 * (the hub for the card at a reader, the serial console for the PIN).
 *
 * Every event function returns an auth_result_t telling the caller what to
 * show / log; AUTH_NONE means "nothing happened" (e.g. an event that does not
 * apply to the current state is simply ignored).
 */
#pragma once

#include "cocis_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Where in the login sequence we are (see the diagram above). */
typedef enum { AUTH_ST_IDLE, AUTH_ST_WAIT_CARD, AUTH_ST_WAIT_PIN, AUTH_ST_SESSION } auth_state_t;

/* Tunable timings and limits, supplied by the caller. */
typedef struct {
    uint32_t step_timeout_ms;   /* time allowed to present the card / type the PIN */
    uint32_t session_ms;        /* how long a login lasts                          */
    uint8_t  max_failures;      /* wrong PINs before lockout                       */
    /* how long arming is refused after too many wrong PINs, ms */
    uint32_t lockout_ms;
} auth_cfg_t;

/* The machine's memory. Initialise with auth_fsm_init(); do not edit fields
 * directly, use the functions below. */
typedef struct {
    /* current state */
    auth_state_t state;
    card_uid_t   uid;           /* admin card that started this login */
    uint32_t     deadline_ms;   /* end of the current step or session */
    /* wrong PINs counted so far; reset by a correct PIN or when a lockout ends */
    uint8_t      failures;
    /* true while a lockout is in force */
    bool         locked;
    /* when the lockout ends, ms (valid only while locked) */
    uint32_t     locked_until_ms;
} auth_fsm_t;

/* Reset to IDLE, not locked, no failures.
 * @param a  the machine to clear */
void          auth_fsm_init(auth_fsm_t *a);

/* Start a login: go to WAIT_CARD and start the step timer.
 * Can be called in any state; it restarts the sequence (an open session
 * is dropped).
 * @param a       the machine
 * @param cfg     timings (step_timeout_ms is used)
 * @param now_ms  current time, ms
 * @return AUTH_LOCKED if a lockout is still running (state unchanged),
 *         otherwise AUTH_NONE */
auth_result_t auth_fsm_arm(auth_fsm_t *a, const auth_cfg_t *cfg, uint32_t now_ms);

/* A card was presented while a login may be waiting for one.
 * @param a         the machine
 * @param cfg       timings (step_timeout_ms is used for the PIN step)
 * @param uid       the card's UID, remembered as the session owner
 * @param is_admin  true if the registry says this card belongs to an
 *                  enabled admin with a PIN set (the caller looks it up)
 * @param now_ms    current time, ms
 * @return AUTH_NONE if not in WAIT_CARD (card ignored),
 *         AUTH_TIMEOUT if the card came after the deadline (-> IDLE),
 *         AUTH_NOT_ADMIN for a non-admin card (-> IDLE),
 *         AUTH_CARD_OK when accepted (-> WAIT_PIN) */
auth_result_t auth_fsm_card(auth_fsm_t *a, const auth_cfg_t *cfg, card_uid_t uid,
                            bool is_admin, uint32_t now_ms);

/* A PIN was entered; the caller has already checked it.
 * @param a       the machine
 * @param cfg     timings (session_ms, max_failures, lockout_ms are used)
 * @param pin_ok  result of the caller's PIN check for the card in a->uid
 * @param now_ms  current time, ms
 * @return AUTH_NONE if not in WAIT_PIN (PIN ignored),
 *         AUTH_TIMEOUT if typed after the deadline (-> IDLE),
 *         AUTH_OK for a correct PIN (-> SESSION),
 *         AUTH_BAD_PIN for a wrong PIN with retries left (stays in WAIT_PIN),
 *         AUTH_LOCKED when that was the last allowed failure (-> IDLE, locked) */
auth_result_t auth_fsm_pin(auth_fsm_t *a, const auth_cfg_t *cfg, bool pin_ok, uint32_t now_ms);

/* End the login (or cancel a half-finished one); always goes to IDLE.
 * @param a  the machine
 * @return AUTH_LOGOUT if a session was open, AUTH_NONE if it was only a
 *         half-finished login (or already idle) */
auth_result_t auth_fsm_logout(auth_fsm_t *a);

/* Call periodically: returns AUTH_TIMEOUT once when a step or session expires.
 * Also clears an expired lockout.
 * @param a       the machine
 * @param now_ms  current time, ms
 * @return AUTH_TIMEOUT on the call that notices the expiry (-> IDLE),
 *         AUTH_NONE otherwise (later calls stay quiet because the state is IDLE) */
auth_result_t auth_fsm_tick(auth_fsm_t *a, uint32_t now_ms);

/* Is an admin logged in right now? Gate for every privileged change.
 * Checks the deadline itself, so it is correct even if the tick has not
 * run yet since the session expired.
 * @param a       the machine (not modified)
 * @param now_ms  current time, ms
 * @return true only in SESSION and before the session deadline */
bool auth_fsm_in_session(const auth_fsm_t *a, uint32_t now_ms);

#ifdef __cplusplus
}
#endif
