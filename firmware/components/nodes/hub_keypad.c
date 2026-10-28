/* Entrance keypad: the local admin login (factor 2 = PIN).
 *
 *   A        start admin login  -> "tap admin card"
 *   0-9      PIN digits (shown as *); the 4th digit submits automatically
 *   #        submit early (a short PIN is simply wrong)
 *   *        clear the PIN
 *   D        log out
 *
 * Full login flow (state machine in core_logic/auth_fsm, wrapped by auth):
 *   1. 'A'            -> auth_begin(): auth waits for a card (WAIT_CARD)
 *   2. admin card tap -> hub_access_handle_scan() calls auth_card()
 *                        (WAIT_PIN)
 *   3. PIN digits     -> collected here in s_pin, then auth_pin() checks
 *                        the salted hash -> SESSION, or wrong PIN (3 wrong
 *                        tries = locked 60 s)
 * Each step has 30 s; auth_tick() (hub 1 s tick) expires it.
 *
 * Runs in the hub task only (keys arrive as MSG_KEY in the hub inbox), so
 * s_pin needs no lock.
 */
#include "hub_internal.h"

#include <string.h>

#include "auth.h"
#include "ui.h"
#include "users.h"

/* How long a result message stays on the LCD, ms. */
#define MSG_MS 2500

/* PIN digits typed so far, NUL-terminated (PIN_LEN = 4). */
static char   s_pin[PIN_LEN + 1];
static size_t s_pin_len;

void hub_keypad_reset(void)
{
    s_pin_len = 0;
    s_pin[0]  = '\0';
}

/* "PIN: **__" - stars for typed digits, underscores for the rest */
static void show_pin(void)
{
    /* "PIN: " is 5 chars, so digit i goes at position 5 + i. */
    char line[16] = "PIN: ";
    for (size_t i = 0; i < PIN_LEN; i++) {
        line[5 + i] = i < s_pin_len ? '*' : '_';
    }
    line[5 + PIN_LEN] = '\0';
    ui_message("Admin login", line, 30000);
}

/* Send the typed PIN to auth and show the outcome. The buffer is wiped
 * right after the check, whatever the result, so the PIN does not linger
 * in RAM and the next attempt starts empty. */
static void submit_pin(void)
{
    auth_result_t r = auth_pin(s_pin);
    hub_keypad_reset();
    switch (r) {
    case AUTH_OK:      ui_message("Admin login OK", "Session started", MSG_MS);
                       ui_feedback(UI_SOUND_GRANTED); break;
    /* Wrong PIN: auth stays in WAIT_PIN, so the user can type again. */
    case AUTH_BAD_PIN: ui_message("Wrong PIN", "Try again: 4 digits", MSG_MS);
                       ui_feedback(UI_SOUND_DENIED); break;
    /* Third wrong PIN: auth locks logins for 60 s and logs an ALERT. */
    case AUTH_LOCKED:  ui_message("Too many attempts", "Locked 60 s", MSG_MS);
                       ui_feedback(UI_SOUND_ALARM); break;
    /* PIN typed after the 30 s step deadline. */
    case AUTH_TIMEOUT: ui_message("Login timed out", "Press A to retry", MSG_MS); break;
    /* AUTH_NONE: auth was no longer waiting for a PIN. */
    default:           ui_message("Press A to login", "", MSG_MS); break;
    }
}

/* Key dispatch. 'A' and 'D' work at any time; every other key is ignored
 * unless auth is waiting for a PIN (otherwise stray presses would fill the
 * buffer). The early return makes the remaining branches PIN-only. */
void hub_keypad_handle(char key)
{
    ui_feedback(UI_SOUND_KEY);

    if (key == 'A') {
        hub_keypad_reset();
        /* auth_begin() refuses while a PIN lockout is running. */
        bool locked = auth_begin() == AUTH_LOCKED;
        ui_message("Admin login", locked ? "Locked, wait 60 s" : "Tap admin card", locked ? MSG_MS : 30000);
    } else if (key == 'D') {
        hub_keypad_reset();
        /* Only say "Logged out" if a session was actually running; 'D' also
         * cancels a half-done login silently. */
        if (auth_logout() == AUTH_LOGOUT) {
            ui_message("Admin", "Logged out", MSG_MS);
        }
    } else if (auth_state() != AUTH_ST_WAIT_PIN) {
        return;                                     /* digits only matter while asking for a PIN */
    } else if (key >= '0' && key <= '9') {
        /* No overflow: the buffer is submitted (and emptied) as soon as it
         * holds PIN_LEN digits, so s_pin_len never exceeds PIN_LEN. */
        s_pin[s_pin_len++] = key;
        s_pin[s_pin_len]   = '\0';
        show_pin();
        if (s_pin_len == PIN_LEN) {
            submit_pin();
        }
    } else if (key == '*') {
        hub_keypad_reset();
        show_pin();
    } else if (key == '#') {
        submit_pin();
    }
}
