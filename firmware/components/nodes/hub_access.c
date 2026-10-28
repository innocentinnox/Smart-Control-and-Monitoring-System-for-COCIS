/* Card scan -> facts -> access_decide() -> doors, ledger, ui, audit log. */
#include "hub_internal.h"

#include <stdio.h>

#include "access_policy.h"
#include "auth.h"
#include "clock.h"
#include "config.h"
#include "door.h"
#include "occupancy.h"
#include "sdkconfig.h"
#include "storage.h"
#include "ui.h"
#include "users.h"

#define MSG_MS 2500

static uint32_t s_escort_until;     /* 0 = no escort window */

bool hub_escort_active(void)
{
    return s_escort_until && !time_reached_ms(clock_ms(), s_escort_until);
}

void hub_access_tick(void)
{
    if (s_escort_until && time_reached_ms(clock_ms(), s_escort_until)) {
        s_escort_until = 0;
        ui_message("Escort window", "closed", 2000);
    }
}

/* Gather everything the policy needs about this scan. */
static access_request_t build_request(card_uid_t uid, door_side_t side, const user_t *u, bool known)
{
    sys_config_t cfg;
    config_get(&cfg);
    struct tm t;
    clock_local(&t);

    return (access_request_t){
        .known         = known,
        .enabled       = known && u->enabled,
        .role          = known ? u->role : ROLE_STUDENT,
        .side          = side,
        .inside        = occupancy_is_inside(uid),
        .schedule_open = schedule_is_open(&cfg.schedule, t.tm_wday, t.tm_hour * 60 + t.tm_min),
        .escort_active = hub_escort_active(),
        .lockdown      = cfg.lockdown,
    };
}

static void announce(access_reason_t reason, door_side_t side, const char *name)
{
    char l1[24], l2[24];
    if (access_granted(reason)) {
        snprintf(l1, sizeof(l1), "%s %s", side == SIDE_IN ? "Welcome" : "Goodbye", name);
        snprintf(l2, sizeof(l2), "%s door open", side == SIDE_IN ? "Entry" : "Exit");
        ui_feedback(UI_SOUND_GRANTED);
    } else {
        snprintf(l1, sizeof(l1), "DENIED %s", name);
        snprintf(l2, sizeof(l2), "%s", access_reason_str(reason));
        ui_feedback(UI_SOUND_DENIED);
    }
    ui_message(l1, l2, MSG_MS);
}

void hub_access_handle_scan(card_uid_t uid, door_side_t side)
{
    /* During an admin login the card is the first factor, not a door request */
    if (auth_state() == AUTH_ST_WAIT_CARD) {
        hub_keypad_reset();
        auth_result_t r = auth_card(uid);
        ui_message("Admin login", r == AUTH_CARD_OK ? "Enter 4-digit PIN" : "Not an admin card",
                   r == AUTH_CARD_OK ? 30000 : MSG_MS);
        ui_feedback(r == AUTH_CARD_OK ? UI_SOUND_INFO : UI_SOUND_DENIED);
        return;
    }

    user_t u;
    bool known = users_find(uid, &u);
    const char *name = known ? u.name : "unknown";
    access_request_t req = build_request(uid, side, &u, known);
    access_reason_t reason = access_decide(&req);
    bool granted = access_granted(reason);

    if (granted) {
        occupancy_set(uid, side == SIDE_IN);
        door_open(side, CONFIG_COCIS_DOOR_OPEN_MS);
    }
    hub_count_access(granted);
    cocis_log(LOG_ACCESS, "uid=%08lX name=%s role=%s side=%s ok=%d reason=%s", (unsigned long)uid,
              name, known ? role_str(u.role) : "NONE", side_str(side), granted,
              access_reason_str(reason));
    announce(reason, side, name);

    /* Security leaving after hours escorts the people still inside out */
    if (reason == ACCESS_OK_PRIVILEGED && side == SIDE_OUT && !req.schedule_open) {
        s_escort_until = clock_ms() + CONFIG_COCIS_ESCORT_S * 1000;
        cocis_log(LOG_CONFIG, "key=escort value=%ds by=%08lX", CONFIG_COCIS_ESCORT_S,
                  (unsigned long)uid);
    }
    /* Someone is held inside after hours: security must come to them */
    if (reason == ACCESS_OUTSIDE_HOURS && side == SIDE_OUT) {
        hub_alert("AFTER_HOURS_EXIT", name);
    }
}
