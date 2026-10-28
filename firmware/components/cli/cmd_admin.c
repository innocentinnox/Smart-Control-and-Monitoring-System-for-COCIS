/* Configuration commands. Everything that changes the system requires an
 * admin session (card + PIN, see auth.h); the change is logged with the
 * admin's card UID. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "auth.h"
#include "bus.h"
#include "cli_internal.h"
#include "clock.h"
#include "config.h"
#include "hub.h"
#include "storage.h"
#include "users.h"

/* ---------------- login / logout ---------------- */
static int cmd_login(int argc, char **argv)
{
    if (argc == 1) {
        if (auth_begin() == AUTH_LOCKED) {
            printf("Login locked after failed attempts, try again in a minute.\n");
            return 1;
        }
        printf("Tap the ADMIN card (Card 4) at the entrance within 30 s,\n"
               "then type 'login <PIN>' here (or the 4-digit PIN on the keypad).\n");
        return 0;
    }
    if (auth_state() != AUTH_ST_WAIT_PIN) {
        printf("Run 'login' and tap the admin card first (two-factor login).\n");
        return 1;
    }
    auth_result_t r = auth_pin(argv[1]);
    printf("%s\n", r == AUTH_OK ? "Logged in: admin session started."
                 : r == AUTH_LOCKED ? "Too many wrong PINs: login locked for 60 s."
                 : "Wrong PIN.");
    return r == AUTH_OK ? 0 : 1;
}

static int cmd_logout(int argc, char **argv)
{
    printf("%s\n", auth_logout() == AUTH_LOGOUT ? "Logged out." : "No active session.");
    return 0;
}

/* ---------------- schedule ---------------- */
static bool parse_hhmm(const char *s, uint16_t *out)
{
    int h, m;
    if (sscanf(s, "%d:%d", &h, &m) != 2 || h < 0 || h > 23 || m < 0 || m > 59) {
        return false;
    }
    *out = (uint16_t)(h * 60 + m);
    return true;
}

/* "weekdays" | "mon-sat" | "all" | 7-char mask like "-MTWTF-" (Sun..Sat) */
static bool parse_days(const char *s, uint8_t *out)
{
    if (strcasecmp(s, "weekdays") == 0) { *out = DAYS_MON_FRI; return true; }
    if (strcasecmp(s, "mon-sat") == 0)  { *out = DAYS_MON_SAT; return true; }
    if (strcasecmp(s, "all") == 0)      { *out = DAYS_ALL;     return true; }
    if (strlen(s) != 7) {
        return false;
    }
    *out = 0;
    for (int d = 0; d < 7; d++) {
        if (s[d] != '-') *out |= 1u << d;
    }
    return true;
}

static int cmd_schedule(int argc, char **argv)
{
    const char *usage = "schedule | schedule set HH:MM HH:MM [weekdays|mon-sat|all|SMTWTFS]";
    sys_config_t cfg;
    config_get(&cfg);

    if (argc == 1) {
        char text[40];
        config_schedule_str(&cfg.schedule, text, sizeof(text));
        printf("Access allowed %s (config v%lu)\n", text, (unsigned long)cfg.version);
        return 0;
    }
    if (argc < 4 || strcmp(argv[1], "set") != 0) {
        return cli_usage(usage);
    }
    if (!cli_require_admin()) {
        return 1;
    }
    access_schedule_t s = cfg.schedule;
    if (!parse_hhmm(argv[2], &s.open_min) || !parse_hhmm(argv[3], &s.close_min) ||
        (argc > 4 && !parse_days(argv[4], &s.days_mask))) {
        return cli_usage(usage);
    }
    esp_err_t err = config_set_schedule(&s, auth_session_uid());
    printf("%s\n", err == ESP_OK ? "Schedule updated." : "Invalid schedule (open == close?).");
    return err == ESP_OK ? 0 : 1;
}

/* ---------------- user management ---------------- */
static int cmd_user(int argc, char **argv)
{
    const char *usage = "user add <UID> <name> <student|staff|security|admin> [PIN]\n"
                        "       user del|enable|disable <UID>";
    card_uid_t uid;
    if (argc < 3 || !card_uid_parse(argv[2], &uid)) {
        return cli_usage(usage);
    }
    if (!cli_require_admin()) {
        return 1;
    }
    card_uid_t by = auth_session_uid();
    esp_err_t err;

    if (strcmp(argv[1], "add") == 0 && argc >= 5) {
        role_t role;
        if (!role_parse(argv[4], &role)) {
            return cli_usage(usage);
        }
        err = users_add(uid, argv[3], role, argc > 5 ? argv[5] : NULL, by);
    } else if (strcmp(argv[1], "del") == 0) {
        err = users_remove(uid, by);
    } else if (strcmp(argv[1], "enable") == 0 || strcmp(argv[1], "disable") == 0) {
        err = users_set_enabled(uid, argv[1][0] == 'e', by);
    } else {
        return cli_usage(usage);
    }
    printf("%s\n", err == ESP_OK ? "Done." : esp_err_to_name(err));
    return err == ESP_OK ? 0 : 1;
}

/* ---------------- room light override ---------------- */
static int cmd_light(int argc, char **argv)
{
    light_mode_t mode;
    int room = argc > 1 ? atoi(argv[1]) : 0;
    if (argc < 3 || room < 1 || room > ROOM_COUNT || !light_mode_parse(argv[2], &mode)) {
        return cli_usage("light <1|2> auto|on|off");
    }
    if (!cli_require_admin()) {
        return 1;
    }
    bus_msg_t m = { .type = MSG_LIGHT_CMD, .light.mode = mode };
    bool sent = bus_send(NODE_HUB, room_to_node(room), &m, pdMS_TO_TICKS(100));
    cocis_log(LOG_CONFIG, "key=light%d value=%s by=%08lX", room, light_mode_str(mode),
              (unsigned long)auth_session_uid());
    printf("%s\n", sent ? "Sent to room node." : "Room node did not accept the command.");
    return sent ? 0 : 1;
}

/* ---------------- lockdown / emergency ---------------- */
static int cmd_lockdown(int argc, char **argv)
{
    if (argc < 2 || (strcmp(argv[1], "on") && strcmp(argv[1], "off"))) {
        return cli_usage("lockdown on|off");
    }
    if (!cli_require_admin()) {
        return 1;
    }
    config_set_lockdown(strcmp(argv[1], "on") == 0, auth_session_uid());
    printf("Lockdown %s.\n", argv[1]);
    return 0;
}

static int cmd_emergency(int argc, char **argv)
{
    if (argc < 2 || strcmp(argv[1], "clear") != 0) {
        printf("Emergency is %s. Use 'emergency clear' (admin) to close the doors again.\n",
               hub_emergency_active() ? "ACTIVE" : "not active");
        return 0;
    }
    if (!cli_require_admin()) {
        return 1;
    }
    hub_emergency_clear(auth_session_uid());
    printf("Emergency cleared, doors back to normal.\n");
    return 0;
}

/* ---------------- clock ---------------- */
static int cmd_time(int argc, char **argv)
{
    if (argc == 1) {
        char now[32];
        clock_format(now, sizeof(now), "%Y-%m-%d %H:%M:%S %a");
        printf("%s (EAT)\n", now);
        return 0;
    }
    struct tm t = { .tm_isdst = -1 };
    if (argc < 4 || strcmp(argv[1], "set") != 0 ||
        sscanf(argv[2], "%d-%d-%d", &t.tm_year, &t.tm_mon, &t.tm_mday) != 3 ||
        sscanf(argv[3], "%d:%d", &t.tm_hour, &t.tm_min) != 2) {
        return cli_usage("time | time set YYYY-MM-DD HH:MM");
    }
    if (!cli_require_admin()) {
        return 1;
    }
    t.tm_year -= 1900;
    t.tm_mon  -= 1;
    esp_err_t err = clock_set(&t);
    cocis_log(LOG_CONFIG, "key=time value=%s_%s by=%08lX", argv[2], argv[3],
              (unsigned long)auth_session_uid());
    printf("%s\n", err == ESP_OK ? "Clock set (system + RTC)." : "System clock set, RTC write failed.");
    return 0;
}

void cli_register_admin_cmds(void)
{
    static const esp_console_cmd_t cmds[] = {
        { .command = "login",     .help = "Two-factor admin login: 'login', tap admin card, 'login <PIN>'", .func = cmd_login },
        { .command = "logout",    .help = "End the admin session",                                   .func = cmd_logout },
        { .command = "schedule",  .help = "Show, or (admin) 'set HH:MM HH:MM [days]' access hours",   .func = cmd_schedule },
        { .command = "user",      .help = "(admin) add/del/enable/disable a card",                   .func = cmd_user },
        { .command = "light",     .help = "(admin) room light override: light <1|2> auto|on|off",    .func = cmd_light },
        { .command = "lockdown",  .help = "(admin) lockdown on|off: only security/admin may pass",   .func = cmd_lockdown },
        { .command = "emergency", .help = "Show break-glass state, (admin) 'emergency clear'",       .func = cmd_emergency },
        { .command = "time",      .help = "Show, or (admin) 'time set YYYY-MM-DD HH:MM'",            .func = cmd_time },
    };
    cli_register(cmds, COUNT_OF(cmds));
}
