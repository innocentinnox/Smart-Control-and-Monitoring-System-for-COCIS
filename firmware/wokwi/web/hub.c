/* Hub task: dispatch, room table, node health, periodic duties.
 *
 * The hub task is an endless loop that:
 *   1. waits (up to 250 ms) for the next message in the hub inbox and hands
 *      it to the right handler (card scan, break-glass, room report),
 *   2. once per second runs the time-based duties (admin login timeouts,
 *      escort window, room node timeouts),
 *   3. every 10 minutes writes an ENERGY record per room.
 *
 * Because only this task makes decisions and writes the room table, there
 * are no races between decisions. Other tasks (CLI, ui) only read, through
 * the query functions at the bottom, which copy the data under s_lock.
 *
 * FreeRTOS reminder: a task is an independent thread with its own stack and
 * priority, started with xTaskCreate(). A queue (here, inside the bus) is a
 * thread-safe FIFO; a task blocked on an empty queue uses no CPU until a
 * message arrives or its timeout expires.
 */
#include "hub_internal.h"

#include <stdio.h>
#include <string.h>

#include "auth.h"
#include "board_pins.h"
#include "buttons.h"
#include "clock.h"
#include "config.h"
#include "door.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "occupancy.h"
#include "rfid.h"
#include "cocis_kconfig.h"
#include "storage.h"
#include "task_config.h"
#include "ui.h"

/* TICK_MS:         period of the housekeeping tick, ms
 * ENERGY_LOG_MS:   one ENERGY record per room every 10 min, ms
 * NODE_TIMEOUT_MS: a room silent this long is marked offline, ms (Kconfig) */
#define TICK_MS            1000
#define ENERGY_LOG_MS      (10 * 60 * 1000)
#define NODE_TIMEOUT_MS    (CONFIG_COCIS_NODE_TIMEOUT_S * 1000)

/* Room table and counters: written by the hub task, read by CLI/ui. */
/* s_rooms index = room - 1 */
static room_status_t     s_rooms[ROOM_COUNT];
static hub_stats_t       s_stats;
/* Mutex: only one task at a time may touch s_rooms / s_stats, so a reader
 * never sees a half-updated record. */
static SemaphoreHandle_t s_lock;
/* Break-glass flag. volatile: read by other tasks (ui, CLI). */
static volatile bool     s_emergency;

/* portMAX_DELAY = wait as long as needed for the mutex. */
#define LOCK()   xSemaphoreTake(s_lock, portMAX_DELAY)
#define UNLOCK() xSemaphoreGive(s_lock)

/* ================= Shared helpers (hub_internal.h) ================= */
void hub_count_access(bool granted)
{
    LOCK();
    granted ? s_stats.granted++ : s_stats.denied++;
    UNLOCK();
}

void hub_alert(const char *code, const char *detail)
{
    LOCK();
    s_stats.alerts++;
    UNLOCK();
    /* Log outside the lock: cocis_log() only queues the record for the
     * storage task, but there is no reason to hold the mutex meanwhile. */
    cocis_log(LOG_ALERT, "code=%s detail=%s", code, detail);
}

/* ================= Room reports ================= */

/* A room sent its full state (MSG_ROOM_STATE): store it in the room table
 * and log what changed.
 * @param room  room number 1..ROOM_COUNT (already range-checked by caller)
 * @param m     the received message (m->room holds the room's state) */
static void handle_room_state(int room, const bus_msg_t *m)
{
    LOCK();
    room_status_t *r = &s_rooms[room - 1];
    /* Compare with the previous record BEFORE overwriting it, to know which
     * log lines to write. A room coming (back) online counts as "changed"
     * so its current state is always logged once. */
    bool was_online   = r->online;
    bool occ_changed  = r->occupied != m->room.occupied || r->daylight != m->room.daylight ||
                        !was_online;
    bool lvl_changed  = r->level != m->room.level || r->mode != m->room.mode || !was_online;
    /* Replace the whole record; receiving a state report also proves the
     * node is alive, so it is online and last_heard_ms is refreshed. */
    *r = (room_status_t){
        .online = true, .occupied = m->room.occupied, .daylight = m->room.daylight, .level = m->room.level,
        .lux = m->room.lux, .mode = m->room.mode, .on_wh = m->room.on_wh,
        .saved_wh = m->room.saved_wh, .last_heard_ms = clock_ms(),
    };
    UNLOCK();

    /* Logging happens after UNLOCK so readers are not held up. */
    if (!was_online) {
        char detail[16];
        snprintf(detail, sizeof(detail), "ROOM%d", room);
        hub_alert("NODE_ONLINE", detail);
    }
    if (occ_changed) {
        cocis_log(LOG_ROOM, "room=%d occupied=%d daylight=%d lux=%u", room, m->room.occupied,
                  m->room.daylight, m->room.lux);
    }
    if (lvl_changed) {
        cocis_log(LOG_LIGHT, "room=%d level=%u mode=%s lux=%u", room, m->room.level,
                  light_mode_str(m->room.mode), m->room.lux);
    }
}

/* A room's heartbeat ("I am still alive"): refresh its last-heard time.
 * Only for a room that is already online: a heartbeat carries no state, so
 * an offline room only comes back online with its next full state report
 * (it then has real data in the table again).
 * @param room  room number 1..ROOM_COUNT */
static void handle_heartbeat(int room)
{
    LOCK();
    if (s_rooms[room - 1].online) {
        s_rooms[room - 1].last_heard_ms = clock_ms();
    }
    UNLOCK();
}

/* A room that stops talking is marked offline once (and keeps running its
 * own lights autonomously: it does not need the hub to switch them).
 * Called every TICK_MS. A room is expired when nothing (state or
 * heartbeat) arrived for NODE_TIMEOUT_MS. Rooms send a heartbeat every 5 s,
 * so the default 15 s timeout tolerates two lost heartbeats. */
static void check_node_timeouts(void)
{
    uint32_t now = clock_ms();
    for (int i = 0; i < ROOM_COUNT; i++) {
        LOCK();
        /* time_reached_ms() is wrap-safe: still correct when the 32-bit
         * millisecond counter rolls over after ~49 days. */
        bool expired = s_rooms[i].online &&
                       time_reached_ms(now, s_rooms[i].last_heard_ms + NODE_TIMEOUT_MS);
        if (expired) {
            /* Clearing online here is what makes the alert fire only once. */
            s_rooms[i].online = false;
        }
        UNLOCK();
        if (expired) {
            char detail[16];
            snprintf(detail, sizeof(detail), "ROOM%d", i + 1);
            hub_alert("NODE_OFFLINE", detail);
            ui_message("Node offline:", detail, 3000);
            ui_feedback(UI_SOUND_INFO);
        }
    }
}

/* Every ENERGY_LOG_MS: write one ENERGY record per online room with its
 * energy used and saved so far (totals since the room booted, as last
 * reported). Offline rooms are skipped: their numbers would be stale. */
static void log_energy(void)
{
    for (int i = 0; i < ROOM_COUNT; i++) {
        room_status_t r;
        if (hub_room_status(i + 1, &r) && r.online) {
            cocis_log(LOG_ENERGY, "room=%d on_wh=%.1f saved_wh=%.1f", i + 1, r.on_wh, r.saved_wh);
        }
    }
}

/* ================= Break-glass ================= */

/* Emergency button pressed: open and hold both doors, raise an alarm.
 * Pressing it again while active does nothing (no repeated alerts). The
 * emergency only ends through hub_emergency_clear() (admin, via the CLI). */
static void handle_break_glass(void)
{
    if (s_emergency) {
        return;
    }
    s_emergency = true;
    door_hold_all(true);
    hub_alert("BREAK_GLASS", "doors_held_open");
    ui_message("!! EMERGENCY !!", "All doors open", 10000);
    ui_feedback(UI_SOUND_ALARM);
}

void hub_emergency_clear(card_uid_t by)
{
    if (!s_emergency) {
        return;
    }
    s_emergency = false;
    door_hold_all(false);
    /* Audit who ended the emergency, e.g. "cleared_by_AABBCCDD". */
    char detail[24];
    snprintf(detail, sizeof(detail), "cleared_by_%08lX", (unsigned long)by);
    hub_alert("BREAK_GLASS", detail);
}

bool hub_emergency_active(void) { return s_emergency; }

/* ================= Inputs -> hub inbox (run in input tasks) ================= */
/* This callback runs in the buttons driver task, NOT in the hub task. It does
 * no work itself: it only posts a message to the hub inbox, so every decision
 * still happens in the hub task, in order. (Card scans arrive the same way,
 * from the rfid task.) */

/* The break-glass button as a one-button group for the buttons driver
 * (must be static: the driver keeps the pointer). */
static const gpio_num_t s_break_glass_pin[] = { PIN_BREAK_GLASS };

/* Buttons callback for the break-glass button.
 * @param index  always 0 (only one button in this group), unused */
static void on_break_glass(size_t index)
{
    bus_msg_t m = { .type = MSG_BREAK_GLASS };
    bus_send(NODE_HUB, NODE_HUB, &m, pdMS_TO_TICKS(50));
}

/* ================= ui status provider ================= */

/* Called by the ui task whenever it redraws the idle LCD screen: fills a
 * snapshot of everything the screen shows. Runs in the ui task, so it only
 * uses thread-safe getters (config, clock, auth, occupancy, room table).
 * @param st  snapshot to fill */
static void ui_status(ui_status_t *st)
{
    sys_config_t cfg;
    config_get(&cfg);
    struct tm t;
    clock_local(&t);

    /* Schedules work in "minutes after midnight", hence hour * 60 + min. */
    st->open      = schedule_is_open(&cfg.schedule, t.tm_wday, t.tm_hour * 60 + t.tm_min);
    st->lockdown  = cfg.lockdown;
    st->emergency = s_emergency;
    st->admin     = auth_is_admin();
    st->inside    = occupancy_count();
    /* Opening hours as "HH:MM-HH:MM" (minutes / 60 = hour, % 60 = minute). */
    snprintf(st->hours, sizeof(st->hours), "%02d:%02d-%02d:%02d",
             cfg.schedule.open_min / 60, cfg.schedule.open_min % 60,
             cfg.schedule.close_min / 60, cfg.schedule.close_min % 60);
    for (int i = 0; i < ROOM_COUNT; i++) {
        room_status_t r;
        hub_room_status(i + 1, &r);
        st->rooms[i].online   = r.online;
        st->rooms[i].occupied = r.occupied;
        st->rooms[i].daylight = r.daylight;
        st->rooms[i].level    = r.level;
    }
}

/* ================= The hub task ================= */

/* The hub task body (never returns).
 * @param arg  unused (xTaskCreate passes NULL) */
static void hub_task(void *arg)
{
    /* Deadlines for the periodic duties: first tick right away, first
     * energy record after ENERGY_LOG_MS. */
    uint32_t next_tick = clock_ms(), next_energy = clock_ms() + ENERGY_LOG_MS;
    bus_msg_t m;

    while (1) {
        /* Step 1: wait for a message, but at most TICK_MS / 4 = 250 ms, so
         * the periodic duties below still run when nothing arrives. */
        if (bus_recv(NODE_HUB, &m, pdMS_TO_TICKS(TICK_MS / 4))) {
            /* Sender -> room number. Input messages come from NODE_HUB
             * itself (room 0), so only room messages pass the 1..ROOM_COUNT
             * check below. */
            int room = node_to_room(m.src);
            switch (m.type) {
            case MSG_CARD_SCAN:   hub_access_handle_scan(m.scan.uid, m.scan.side); break;
            case MSG_BREAK_GLASS: handle_break_glass(); break;
            case MSG_ROOM_STATE:  if (room >= 1 && room <= ROOM_COUNT) handle_room_state(room, &m); break;
            case MSG_HEARTBEAT:   if (room >= 1 && room <= ROOM_COUNT) handle_heartbeat(room); break;
            default: break;
            }
        }

        /* Step 2: once-per-second housekeeping. Deadlines (not delays) keep
         * the rate steady however many messages arrive. */
        uint32_t now = clock_ms();
        if (time_reached_ms(now, next_tick)) {
            next_tick = now + TICK_MS;
            /* Expire admin login steps / session, close the escort window
             * when due, mark silent rooms offline. */
            auth_tick();
            hub_access_tick();
            check_node_timeouts();
        }
        /* Step 3: periodic energy record. */
        if (time_reached_ms(now, next_energy)) {
            next_energy = now + ENERGY_LOG_MS;
            log_energy();
        }
    }
}

void hub_start(void)
{
    /* The mutex must exist before anything can call the query functions. */
    s_lock = xSemaphoreCreateMutex();
    door_init();
    /* ui gets our status provider so it can draw the idle screen. */
    ui_init(ui_status);
    /* Input devices: their callbacks only post messages to the hub inbox. */
    rfid_init();
    buttons_add_group(s_break_glass_pin, 1, on_break_glass);
    /* Start the task last, when everything it uses is ready. */
    xTaskCreate(hub_task, "hub", TASK_HUB_STACK, NULL, TASK_HUB_PRIO, NULL);
}

/* ================= Queries (any task) ================= */
bool hub_room_status(int room, room_status_t *out)
{
    if (room < 1 || room > ROOM_COUNT) {
        return false;
    }
    /* Copy under the lock so the caller gets a consistent record. */
    LOCK();
    *out = s_rooms[room - 1];
    UNLOCK();
    return true;
}

void hub_stats(hub_stats_t *out)
{
    LOCK();
    *out = s_stats;
    UNLOCK();
}
