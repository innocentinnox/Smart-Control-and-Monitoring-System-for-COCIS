/* Entrance keypad: the local admin login (factor 2 = PIN).
 *
 *   A        start admin login  -> "tap admin card"
 *   0-9      PIN digits (shown as *)
 *   *        clear the PIN
 *   #        submit the PIN
 *   D        log out
 */
#include "hub_internal.h"

#include <string.h>

#include "auth.h"
#include "ui.h"
#include "users.h"

#define MSG_MS 2500

static char   s_pin[PIN_MAX_LEN + 1];
static size_t s_pin_len;

void hub_keypad_reset(void)
{
    s_pin_len = 0;
    s_pin[0]  = '\0';
}

static void show_pin(void)
{
    char stars[PIN_MAX_LEN + 8] = "PIN: ";
    memset(stars + 5, '*', s_pin_len);
    stars[5 + s_pin_len] = '\0';
    ui_message("Admin login", stars, 30000);
}

static void submit_pin(void)
{
    auth_result_t r = auth_pin(s_pin);
    hub_keypad_reset();
    switch (r) {
    case AUTH_OK:      ui_message("Admin login OK", "Session started", MSG_MS);
                       ui_feedback(UI_SOUND_GRANTED); break;
    case AUTH_BAD_PIN: ui_message("Wrong PIN", "Try again", MSG_MS);
                       ui_feedback(UI_SOUND_DENIED); break;
    case AUTH_LOCKED:  ui_message("Too many attempts", "Locked 60 s", MSG_MS);
                       ui_feedback(UI_SOUND_ALARM); break;
    default:           ui_message("Press A to login", "", MSG_MS); break;
    }
}

void hub_keypad_handle(char key)
{
    ui_feedback(UI_SOUND_KEY);
    auth_state_t st = auth_state();

    if (key == 'A') {
        hub_keypad_reset();
        bool locked = auth_begin() == AUTH_LOCKED;
        ui_message("Admin login", locked ? "Locked, wait 60 s" : "Tap admin card", locked ? MSG_MS : 30000);
    } else if (key == 'D') {
        hub_keypad_reset();
        if (auth_logout() == AUTH_LOGOUT) {
            ui_message("Admin", "Logged out", MSG_MS);
        }
    } else if (st != AUTH_ST_WAIT_PIN) {
        return;                                     /* digits only matter while asking for a PIN */
    } else if (key >= '0' && key <= '9') {
        if (s_pin_len < PIN_MAX_LEN) {
            s_pin[s_pin_len++] = key;
            s_pin[s_pin_len]   = '\0';
        }
        show_pin();
    } else if (key == '*') {
        hub_keypad_reset();
        show_pin();
    } else if (key == '#') {
        submit_pin();
    }
}
