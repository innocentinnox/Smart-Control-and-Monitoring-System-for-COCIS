/* Two-factor admin login state machine (pure logic, no I/O).
 *
 *   IDLE --arm--> WAIT_CARD --admin card--> WAIT_PIN --correct PIN--> SESSION
 *     ^              |  non-admin card / 30 s     |  wrong PIN x3 -> locked 60 s
 *     +--------------+--------------------------- +-------- session expires / logout
 *
 * Factor 1 = something you have (admin RFID card), factor 2 = something you
 * know (PIN, typed on the keypad or the serial console). Only during a SESSION
 * may the schedule, users, clock or light overrides be changed.
 *
 * The PIN itself is checked by the caller (users_verify_pin); this module only
 * gets a yes/no, which keeps it free of crypto and storage.
 */
#pragma once

#include "cocis_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { AUTH_ST_IDLE, AUTH_ST_WAIT_CARD, AUTH_ST_WAIT_PIN, AUTH_ST_SESSION } auth_state_t;

typedef struct {
    uint32_t step_timeout_ms;   /* time allowed to present the card / type the PIN */
    uint32_t session_ms;        /* how long a login lasts                          */
    uint8_t  max_failures;      /* wrong PINs before lockout                       */
    uint32_t lockout_ms;
} auth_cfg_t;

typedef struct {
    auth_state_t state;
    card_uid_t   uid;           /* admin card that started this login */
    uint32_t     deadline_ms;   /* end of the current step or session */
    uint8_t      failures;
    bool         locked;
    uint32_t     locked_until_ms;
} auth_fsm_t;

void          auth_fsm_init(auth_fsm_t *a);
auth_result_t auth_fsm_arm(auth_fsm_t *a, const auth_cfg_t *cfg, uint32_t now_ms);
auth_result_t auth_fsm_card(auth_fsm_t *a, const auth_cfg_t *cfg, card_uid_t uid,
                            bool is_admin, uint32_t now_ms);
auth_result_t auth_fsm_pin(auth_fsm_t *a, const auth_cfg_t *cfg, bool pin_ok, uint32_t now_ms);
auth_result_t auth_fsm_logout(auth_fsm_t *a);

/* Call periodically: returns AUTH_TIMEOUT once when a step or session expires. */
auth_result_t auth_fsm_tick(auth_fsm_t *a, uint32_t now_ms);

bool auth_fsm_in_session(const auth_fsm_t *a, uint32_t now_ms);

#ifdef __cplusplus
}
#endif
