/* Entrance keypad: the local admin login (factor 2 = PIN).
 *
 *   A        start admin login  -> "tap admin card"
 *   0-9      PIN digits (shown as *); the 4th digit submits automatically
 *   #        submit early (a short PIN is simply wrong)
 *   *        clear the PIN
 *   D        log out
 */
#include "hub_internal.h"

#include <string.h>

#include "auth.h"
#include "ui.h"
#include "users.h"

#define MSG_MS 2500

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
    char line[16] = "PIN: ";
    for (size_t i = 0; i < PIN_LEN; i++) {
        line[5 + i] = i < s_pin_len ? '*' : '_';
    }
    line[5 + PIN_LEN] = '\0';
    ui_message("Admin login", line, 30000);
}

static void submit_pin(void)
{
    auth_result_t r = auth_pin(s_pin);
    hub_keypad_reset();
    switch (r) {
    case AUTH_OK:      ui_message("Admin login OK", "Session started", MSG_MS);
                       ui_feedback(UI_SOUND_GRANTED); break;
    case AUTH_BAD_PIN: ui_message("Wrong PIN", "Try again: 4 digits", MSG_MS);
                       ui_feedback(UI_SOUND_DENIED); break;
    case AUTH_LOCKED:  ui_message("Too many attempts", "Locked 60 s", MSG_MS);
                       ui_feedback(UI_SOUND_ALARM); break;
    case AUTH_TIMEOUT: ui_message("Login timed out", "Press A to retry", MSG_MS); break;
    default:           ui_message("Press A to login", "", MSG_MS); break;
    }
}

void hub_keypad_handle(char key)
{
    ui_feedback(UI_SOUND_KEY);

    if (key == 'A') {
        hub_keypad_reset();
        bool locked = auth_begin() == AUTH_LOCKED;
        ui_message("Admin login", locked ? "Locked, wait 60 s" : "Tap admin card", locked ? MSG_MS : 30000);
    } else if (key == 'D') {
        hub_keypad_reset();
        if (auth_logout() == AUTH_LOGOUT) {
            ui_message("Admin", "Logged out", MSG_MS);
        }
    } else if (auth_state() != AUTH_ST_WAIT_PIN) {
        return;                                     /* digits only matter while asking for a PIN */
    } else if (key >= '0' && key <= '9') {
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
