/* Admin authentication service: auth_fsm (core_logic) + PIN check + audit.
 *
 * auth_fsm is the pure state machine (IDLE -> WAIT_CARD -> WAIT_PIN ->
 * SESSION, see auth_fsm.h). This module owns the one shared instance of it
 * and adds what the pure logic leaves out: the clock, the user registry
 * (is this card an admin? is the PIN right?) and the audit log.
 *
 * Thread-safe wrapper used by the hub (admin card at a reader) and the CLI
 * (login/logout, privileged commands). Every outcome is logged as an AUTH
 * event, and a PIN lockout also raises an ALERT.
 *
 * Timings: 30 s for each login step, 3 wrong PINs -> 60 s lockout, session
 * length CONFIG_COCIS_ADMIN_SESSION_S (menuconfig, default 300 s).
 */
#pragma once

#include "auth_fsm.h"

/* Create the mutex and reset the state machine to IDLE. Call once at boot,
 * before the hub or CLI start. */
void auth_init(void);

/* Start a login: the FSM now waits up to 30 s for an admin card.
 * @return AUTH_NONE when armed, AUTH_LOCKED during a PIN lockout. */
auth_result_t auth_begin(void);                 /* "I want to log in": wait for a card */

/* A card was tapped. Only has an effect while waiting for a card.
 * The card counts as admin if it is registered, enabled, has ROLE_ADMIN and
 * a PIN.
 * @param uid  tapped card.
 * @return AUTH_CARD_OK (now waiting for PIN), AUTH_NOT_ADMIN, AUTH_TIMEOUT,
 *         or AUTH_NONE if no login was in progress (normal door tap). */
auth_result_t auth_card(card_uid_t uid);        /* card tapped while waiting           */

/* A PIN was entered. Only has an effect while waiting for the PIN.
 * @param pin  typed PIN text (checked against the card from auth_card()).
 * @return AUTH_OK (session started), AUTH_BAD_PIN (retry allowed),
 *         AUTH_LOCKED (3rd failure), AUTH_TIMEOUT, or AUTH_NONE. */
auth_result_t auth_pin(const char *pin);        /* PIN typed on the console            */

/* End the session (or abandon a login in progress).
 * @return AUTH_LOGOUT if a session was active, else AUTH_NONE. */
auth_result_t auth_logout(void);

/* Periodic housekeeping, called by the hub task: ends a step or session
 * whose deadline passed (logged as TIMEOUT) and lets a lockout expire. */
void          auth_tick(void);                  /* expire steps / sessions             */

/* Current FSM state (e.g. the CLI checks for AUTH_ST_WAIT_PIN). */
auth_state_t auth_state(void);

/* @return true while an admin session is active and not yet expired.
 * Every privileged CLI command checks this (via cli_require_admin()). */
bool         auth_is_admin(void);               /* an admin session is active          */

/* @return admin card of the active session, used as "by=" in audit logs. */
card_uid_t   auth_session_uid(void);            /* who is logged in (or NONE)          */

/* @return whole seconds left in the session, 0 if none. */
uint32_t     auth_session_left_s(void);
