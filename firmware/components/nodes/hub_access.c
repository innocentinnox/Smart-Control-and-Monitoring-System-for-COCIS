/* Card scan -> facts -> access_decide() -> doors, ledger, ui, audit log.
 *
 * Runs in the hub task only. The flow of one card scan:
 *
 *   1. Is an admin login waiting for a card? Then this card is the login's
 *      first factor, not a door request -> hand it to auth and stop.
 *   2. Look the card up in the user registry.
 *   3. Collect every fact the rules need (build_request()).
 *   4. Ask the pure policy for a verdict (access_decide()).
 *   5. If granted: update the "who is inside" ledger and open that door.
 *   6. Count it, write an ACCESS record to the audit log.
 *   7. Tell the person: LCD message + buzzer (announce()).
 *   8. Special after-hours cases: open an escort window, or raise an alert.
 *
 * The rules themselves are in core_logic/access_policy.c; this file only
 * gathers inputs and carries out the verdict.
 */
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

/* How long a granted/denied message stays on the LCD, ms. */
#define MSG_MS 2500

/* End of the after-hours escort window as a clock_ms() deadline.
 * Written only by the hub task; read by the CLI too (a 32-bit read is atomic). */
static uint32_t s_escort_until;     /* 0 = no escort window */

bool hub_escort_active(void)
{
    /* Active = a window was opened AND its deadline has not passed yet. */
    return s_escort_until && !time_reached_ms(clock_ms(), s_escort_until);
}

void hub_access_tick(void)
{
    /* When the deadline passes, reset to 0 (so this fires only once) and
     * tell the people at the door. */
    if (s_escort_until && time_reached_ms(clock_ms(), s_escort_until)) {
        s_escort_until = 0;
        ui_message("Escort window", "closed", 2000);
    }
}

/* Gather everything the policy needs about this scan.
 * The result is a plain struct of facts, so access_decide() can stay pure
 * (no I/O, unit-tested on a PC).
 * @param uid    card UID (to ask the occupancy ledger if they are inside)
 * @param side   reader the card was held at (SIDE_IN / SIDE_OUT)
 * @param u      the user record; only read when `known` is true
 * @param known  true if the card is in the user registry
 * @return the filled-in access request. */
static access_request_t build_request(card_uid_t uid, door_side_t side, const user_t *u, bool known)
{
    /* Snapshot of the settings (schedule, lockdown) and the local time. */
    sys_config_t cfg;
    config_get(&cfg);
    struct tm t;
    clock_local(&t);

    return (access_request_t){
        .known         = known,
        /* For an unknown card u is not filled in, so never read it:
         * `known &&` short-circuits, and role gets a harmless placeholder
         * (an unknown card is refused by the first rule anyway). */
        .enabled       = known && u->enabled,
        .role          = known ? u->role : ROLE_STUDENT,
        .side          = side,
        .inside        = occupancy_is_inside(uid),
        /* Schedule uses "minutes after midnight" and tm_wday (0 = Sunday). */
        .schedule_open = schedule_is_open(&cfg.schedule, t.tm_wday, t.tm_hour * 60 + t.tm_min),
        .escort_active = hub_escort_active(),
        .lockdown      = cfg.lockdown,
    };
}

/* Show the verdict to the person at the door: two LCD lines for MSG_MS and
 * a sound.
 *   granted: "Welcome <name>" / "Entry door open"    (entry side)
 *            "Goodbye <name>" / "Exit door open"     (exit side), happy beep
 *   denied:  "DENIED <name>"  / reason, e.g. "ANTI_PASSBACK", three low beeps
 * @param reason  verdict from access_decide()
 * @param side    reader used (picks Welcome/Goodbye and Entry/Exit)
 * @param name    user name, or "unknown" for an unregistered card
 * Lines are built with snprintf into 24-byte buffers, so a long name is
 * cut off safely instead of overflowing; the LCD itself shows 20 chars. */
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
    /* Step 1. During an admin login the card is the first factor, not a door
     * request. The user pressed 'A' on the keypad, so auth is in WAIT_CARD:
     * pass the card to auth (which checks it is an enabled ADMIN card with a
     * PIN), then ask for the PIN or say no. No door opens in this case, and
     * the reader side does not matter. Any old PIN digits are discarded.
     * The PIN prompt stays 30 s (the time allowed to type it). */
    if (auth_state() == AUTH_ST_WAIT_CARD) {
        hub_keypad_reset();
        auth_result_t r = auth_card(uid);
        ui_message("Admin login", r == AUTH_CARD_OK ? "Enter 4-digit PIN" : "Not an admin card",
                   r == AUTH_CARD_OK ? 30000 : MSG_MS);
        ui_feedback(r == AUTH_CARD_OK ? UI_SOUND_INFO : UI_SOUND_DENIED);
        return;
    }

    /* Step 2. Who is this? users_find() copies the record into u. */
    user_t u;
    bool known = users_find(uid, &u);
    const char *name = known ? u.name : "unknown";
    /* Steps 3-4. Facts -> verdict. reason says WHY (also for the log);
     * access_granted() turns it into yes/no. */
    access_request_t req = build_request(uid, side, &u, known);
    access_reason_t reason = access_decide(&req);
    bool granted = access_granted(reason);

    /* Step 5. Act on a grant. The ledger is updated at the same time as the
     * door opens: entering marks the person inside, exiting marks them
     * outside. That is what anti-passback checks on the next scan. */
    if (granted) {
        occupancy_set(uid, side == SIDE_IN);
        door_open(side, CONFIG_COCIS_DOOR_OPEN_MS);
    }
    /* Step 6. Statistics and the tamper-evident audit record, e.g.
     * "uid=A1B2C3D4 name=Alice role=STUDENT side=IN ok=1 reason=OK". */
    hub_count_access(granted);
    cocis_log(LOG_ACCESS, "uid=%08lX name=%s role=%s side=%s ok=%d reason=%s", (unsigned long)uid,
              name, known ? role_str(u.role) : "NONE", side_str(side), granted,
              access_reason_str(reason));
    /* Step 7. LCD + buzzer feedback. */
    announce(reason, side, name);

    /* Step 8a. Security leaving after hours escorts the people still inside
     * out: for CONFIG_COCIS_ESCORT_S seconds, ordinary users may also exit
     * (access_decide() returns ACCESS_OK_ESCORT for them). Logged as a
     * CONFIG event because it temporarily changes the access rules. */
    if (reason == ACCESS_OK_PRIVILEGED && side == SIDE_OUT && !req.schedule_open) {
        s_escort_until = clock_ms() + CONFIG_COCIS_ESCORT_S * 1000;
        cocis_log(LOG_CONFIG, "key=escort value=%ds by=%08lX", CONFIG_COCIS_ESCORT_S,
                  (unsigned long)uid);
    }
    /* Step 8b. Someone is held inside after hours: security must come to them */
    if (reason == ACCESS_OUTSIDE_HOURS && side == SIDE_OUT) {
        hub_alert("AFTER_HOURS_EXIT", name);
    }
}
