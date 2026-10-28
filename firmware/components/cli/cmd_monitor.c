/* Read-only monitoring commands: anyone at the console may use them.
 *
 * They only read: every service returns copies under its own mutex
 * (config_get, hub_stats, occupancy_snapshot, users_snapshot), so printing
 * never blocks the hub for long and never sees half-updated data.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "auth.h"
#include "bus.h"
#include "cli_internal.h"
#include "clock.h"
#include "config.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "hub.h"
#include "occupancy.h"
#include "storage.h"
#include "users.h"

/* "status": one-screen overview of the whole system (time, schedule,
 * modes, occupancy, counters, storage, bus, admin session, rooms). */
static int cmd_status(int argc, char **argv)
{
    char now[32], sched[40];
    sys_config_t cfg;
    hub_stats_t st;
    struct tm t;

    clock_format(now, sizeof(now), "%Y-%m-%d %H:%M:%S %a");
    config_get(&cfg);
    config_schedule_str(&cfg.schedule, sched, sizeof(sched));
    hub_stats(&st);
    clock_local(&t);
    /* >= 0: building is open, value = minutes until closing; -1: closed. */
    int to_close = schedule_minutes_to_close(&cfg.schedule, t.tm_wday, t.tm_hour * 60 + t.tm_min);

    printf("Time        : %s (EAT)\n", now);
    /* esp_timer_get_time() is microseconds since boot. */
    printf("Uptime      : %llu s, free heap %lu B\n", esp_timer_get_time() / 1000000ULL,
           (unsigned long)esp_get_free_heap_size());
    printf("Schedule    : %s (v%lu)  -> %s", sched, (unsigned long)cfg.version,
           to_close >= 0 ? "OPEN" : "CLOSED");
    if (to_close >= 0) printf(", closes in %d min", to_close);
    printf("\n");
    printf("Lockdown    : %s   Emergency: %s   Escort window: %s\n", cfg.lockdown ? "ON" : "off",
           hub_emergency_active() ? "ACTIVE" : "off", hub_escort_active() ? "open" : "closed");
    printf("Inside      : %u people\n", (unsigned)occupancy_count());
    printf("Access      : %lu granted, %lu denied, %lu alerts\n", (unsigned long)st.granted,
           (unsigned long)st.denied, (unsigned long)st.alerts);
    printf("Storage     : %s, last seq %lu\n", storage_sd_ok() ? "SD card OK" : "RAM ONLY (no SD)",
           (unsigned long)storage_last_seq());
    printf("Bus         : %lu duplicate frames rejected\n", (unsigned long)bus_dropped_count());
    printf("Admin       : %s", auth_is_admin() ? "logged in" : "not logged in");
    if (auth_is_admin()) printf(" as %08lX (%lu s left)", (unsigned long)auth_session_uid(),
                                (unsigned long)auth_session_left_s());
    printf("\n");
    /* Rooms are numbered 1..ROOM_COUNT; data is the hub's latest report. */
    for (int r = 1; r <= ROOM_COUNT; r++) {
        room_status_t rs;
        hub_room_status(r, &rs);
        printf("Room %d      : %s, %s, light %3u%% (%s), %u lux\n", r,
               rs.online ? "online" : "OFFLINE", rs.occupied ? "occupied" : "vacant", rs.level,
               light_mode_str(rs.mode), rs.lux);
    }
    return 0;
}

/* "who": muster list - everyone the occupancy ledger says is inside, with
 * name, role and entry time. */
static int cmd_who(int argc, char **argv)
{
    /* static: a 64-entry array is kept off the console task's stack. */
    static presence_t people[OCCUPANCY_MAX];
    size_t n = occupancy_snapshot(people, OCCUPANCY_MAX);
    printf("%u people inside the building:\n", (unsigned)n);
    for (size_t i = 0; i < n; i++) {
        /* The ledger only stores UIDs; names come from the user registry.
         * A card deleted while inside is shown as "?". */
        user_t u;
        bool known = users_find(people[i].uid, &u);
        struct tm t;
        char since[20];
        localtime_r(&people[i].since, &t);
        strftime(since, sizeof(since), "%Y-%m-%d %H:%M", &t);
        printf("  %08lX  %-15s %-9s since %s\n", (unsigned long)people[i].uid,
               known ? u.name : "?", known ? role_str(u.role) : "?", since);
    }
    return 0;
}

/* "rooms": table of each room's link, occupancy, light level and mode,
 * light sensor reading, and energy (Wh used vs. saved compared to lights
 * always on during opening hours). */
static int cmd_rooms(int argc, char **argv)
{
    printf("Room  Link     Occupancy  Light  Mode  Lux    Used Wh  Saved Wh\n");
    for (int r = 1; r <= ROOM_COUNT; r++) {
        room_status_t rs;
        hub_room_status(r, &rs);
        printf("%-5d %-8s %-10s %4u%%  %-5s %-6u %8.1f %9.1f\n", r, rs.online ? "online" : "OFFLINE",
               rs.occupied ? "occupied" : "vacant", rs.level, light_mode_str(rs.mode), rs.lux,
               rs.on_wh, rs.saved_wh);
    }
    return 0;
}

/* "users": table of all registered cards. Only shows whether a PIN is set,
 * never the hash. */
static int cmd_users(int argc, char **argv)
{
    /* static: 32 user records are too big for the console task's stack. */
    static user_t users[USERS_MAX];
    size_t n = users_snapshot(users, USERS_MAX);
    printf("UID       Name            Role      State    PIN  Inside\n");
    for (size_t i = 0; i < n; i++) {
        printf("%08lX  %-15s %-9s %-8s %-4s %s\n", (unsigned long)users[i].uid, users[i].name,
               role_str(users[i].role), users[i].enabled ? "enabled" : "DISABLED",
               users[i].has_pin ? "yes" : "-", occupancy_is_inside(users[i].uid) ? "yes" : "");
    }
    return 0;
}

/* "log tail [n]"           -> print the last n lines of today's log (default 10)
 * "log verify [YYYYMMDD]"  -> re-check the hash chain of a day's file
 *                             (default today) to detect edited lines
 * "log echo on|off"        -> show / hide new log lines on the console */
static int cmd_log(int argc, char **argv)
{
    const char *usage = "log tail [n] | log verify [YYYYMMDD] | log echo on|off";
    if (argc < 2) {
        return cli_usage(usage);
    }
    if (strcmp(argv[1], "tail") == 0) {
        int n = argc > 2 ? atoi(argv[2]) : 10;
        if (storage_tail(n) == 0) printf("(no entries today)\n");
        return 0;
    }
    if (strcmp(argv[1], "verify") == 0) {
        /* lines = number checked (-1 = no file); bad = first broken line
         * (1-based) or 0 if every line's hash matches. */
        int bad = 0;
        int lines = storage_verify(argc > 2 ? argv[2] : NULL, &bad);
        if (lines < 0) {
            printf("No log file for that day (or no SD card).\n");
            return 1;
        }
        if (bad) {
            printf("TAMPERED: hash chain broken at line %d (of %d checked).\n", bad, lines);
            return 1;
        }
        printf("OK: %d lines, hash chain intact.\n", lines);
        return 0;
    }
    /* Anything other than "on" (e.g. "off") turns the echo off. */
    if (strcmp(argv[1], "echo") == 0 && argc > 2) {
        storage_set_echo(strcmp(argv[2], "on") == 0);
        return 0;
    }
    return cli_usage(usage);
}

/* Command table (name, "help" text, handler) registered with esp_console. */
void cli_register_monitor_cmds(void)
{
    static const esp_console_cmd_t cmds[] = {
        { .command = "status", .help = "System overview: time, schedule, occupancy, rooms, storage", .func = cmd_status },
        { .command = "who",    .help = "People currently inside the building (muster list)",          .func = cmd_who },
        { .command = "rooms",  .help = "Room occupancy, light levels and energy",                     .func = cmd_rooms },
        { .command = "users",  .help = "Registered cards",                                            .func = cmd_users },
        { .command = "log",    .help = "Event log: tail [n] | verify [YYYYMMDD] | echo on|off",       .func = cmd_log },
    };
    cli_register(cmds, COUNT_OF(cmds));
}
