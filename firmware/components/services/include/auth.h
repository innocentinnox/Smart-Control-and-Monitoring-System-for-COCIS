/* Admin authentication service: auth_fsm (core_logic) + PIN check + audit.
 *
 * Thread-safe wrapper used by the hub (card taps, keypad) and the CLI
 * (login/logout, privileged commands). Every outcome is logged as an AUTH
 * event, and a PIN lockout also raises an ALERT.
 */
#pragma once

#include "auth_fsm.h"

void auth_init(void);

auth_result_t auth_begin(void);                 /* "I want to log in": wait for a card */
auth_result_t auth_card(card_uid_t uid);        /* card tapped while waiting           */
auth_result_t auth_pin(const char *pin);        /* PIN typed (keypad or CLI)           */
auth_result_t auth_logout(void);
void          auth_tick(void);                  /* expire steps / sessions             */

auth_state_t auth_state(void);
bool         auth_is_admin(void);               /* an admin session is active          */
card_uid_t   auth_session_uid(void);            /* who is logged in (or NONE)          */
uint32_t     auth_session_left_s(void);
