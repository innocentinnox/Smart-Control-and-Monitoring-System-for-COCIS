/* Hub task: dispatch, room table, node health, periodic duties. */
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
#include "keypad.h"
#include "occupancy.h"
#include "rfid.h"
#include "sdkconfig.h"
#include "storage.h"
#include "task_config.h"
#include "ui.h"

#define TICK_MS            1000
#define ENERGY_LOG_MS      (10 * 60 * 1000)
#define NODE_TIMEOUT_MS    (CONFIG_COCIS_NODE_TIMEOUT_S * 1000)

/* Room table and counters: written by the hub task, read by CLI/ui. */
static room_status_t     s_rooms[ROOM_COUNT];
static hub_stats_t       s_stats;
static SemaphoreHandle_t s_lock;
static volatile bool     s_emergency;

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
    cocis_log(LOG_ALERT, "code=%s detail=%s", code, detail);
}

/* ================= Room reports ================= */
static void handle_room_state(int room, const bus_msg_t *m)
{
    LOCK();
    room_status_t *r = &s_rooms[room - 1];
    bool was_online   = r->online;
    bool occ_changed  = r->occupied != m->room.occupied || !was_online;
    bool lvl_changed  = r->level != m->room.level || r->mode != m->room.mode || !was_online;
    *r = (room_status_t){
        .online = true, .occupied = m->room.occupied, .level = m->room.level,
        .lux = m->room.lux, .mode = m->room.mode, .on_wh = m->room.on_wh,
        .saved_wh = m->room.saved_wh, .last_heard_ms = clock_ms(),
    };
    UNLOCK();

    if (!was_online) {
        char detail[16];
        snprintf(detail, sizeof(detail), "ROOM%d", room);
        hub_alert("NODE_ONLINE", detail);
    }
    if (occ_changed) {
        cocis_log(LOG_ROOM, "room=%d occupied=%d", room, m->room.occupied);
    }
    if (lvl_changed) {
        cocis_log(LOG_LIGHT, "room=%d level=%u mode=%s lux=%u", room, m->room.level,
                  light_mode_str(m->room.mode), m->room.lux);
    }
}

static void handle_heartbeat(int room)
{
    LOCK();
    if (s_rooms[room - 1].online) {
        s_rooms[room - 1].last_heard_ms = clock_ms();
    }
    UNLOCK();
}

/* A room that stops talking is marked offline once (and keeps running its
 * own lights autonomously: it does not need the hub to switch them). */
static void check_node_timeouts(void)
{
    uint32_t now = clock_ms();
    for (int i = 0; i < ROOM_COUNT; i++) {
        LOCK();
        bool expired = s_rooms[i].online &&
                       time_reached_ms(now, s_rooms[i].last_heard_ms + NODE_TIMEOUT_MS);
        if (expired) {
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
    char detail[24];
    snprintf(detail, sizeof(detail), "cleared_by_%08lX", (unsigned long)by);
    hub_alert("BREAK_GLASS", detail);
}

bool hub_emergency_active(void) { return s_emergency; }

/* ================= Inputs -> hub inbox (run in input tasks) ================= */
static void on_key(char key)
{
    bus_msg_t m = { .type = MSG_KEY, .key.key = key };
    bus_send(NODE_HUB, NODE_HUB, &m, pdMS_TO_TICKS(50));
}

static const gpio_num_t s_break_glass_pin[] = { PIN_BREAK_GLASS };

static void on_break_glass(size_t index)
{
    bus_msg_t m = { .type = MSG_BREAK_GLASS };
    bus_send(NODE_HUB, NODE_HUB, &m, pdMS_TO_TICKS(50));
}

/* ================= ui status provider ================= */
static void ui_status(ui_status_t *st)
{
    sys_config_t cfg;
    config_get(&cfg);
    struct tm t;
    clock_local(&t);

    st->open      = schedule_is_open(&cfg.schedule, t.tm_wday, t.tm_hour * 60 + t.tm_min);
    st->lockdown  = cfg.lockdown;
    st->emergency = s_emergency;
    st->admin     = auth_is_admin();
    st->inside    = occupancy_count();
    snprintf(st->hours, sizeof(st->hours), "%02d:%02d-%02d:%02d",
             cfg.schedule.open_min / 60, cfg.schedule.open_min % 60,
             cfg.schedule.close_min / 60, cfg.schedule.close_min % 60);
    for (int i = 0; i < ROOM_COUNT; i++) {
        room_status_t r;
        hub_room_status(i + 1, &r);
        st->rooms[i].online   = r.online;
        st->rooms[i].occupied = r.occupied;
        st->rooms[i].level    = r.level;
    }
}

/* ================= The hub task ================= */
static void hub_task(void *arg)
{
    uint32_t next_tick = clock_ms(), next_energy = clock_ms() + ENERGY_LOG_MS;
    bus_msg_t m;

    while (1) {
        if (bus_recv(NODE_HUB, &m, pdMS_TO_TICKS(TICK_MS / 4))) {
            int room = node_to_room(m.src);
            switch (m.type) {
            case MSG_CARD_SCAN:   hub_access_handle_scan(m.scan.uid, m.scan.side); break;
            case MSG_KEY:         hub_keypad_handle(m.key.key); break;
            case MSG_BREAK_GLASS: handle_break_glass(); break;
            case MSG_ROOM_STATE:  if (room >= 1 && room <= ROOM_COUNT) handle_room_state(room, &m); break;
            case MSG_HEARTBEAT:   if (room >= 1 && room <= ROOM_COUNT) handle_heartbeat(room); break;
            default: break;
            }
        }

        uint32_t now = clock_ms();
        if (time_reached_ms(now, next_tick)) {
            next_tick = now + TICK_MS;
            auth_tick();
            hub_access_tick();
            check_node_timeouts();
        }
        if (time_reached_ms(now, next_energy)) {
            next_energy = now + ENERGY_LOG_MS;
            log_energy();
        }
    }
}

void hub_start(void)
{
    s_lock = xSemaphoreCreateMutex();
    door_init();
    ui_init(ui_status);
    rfid_init();
    keypad_init(on_key);
    buttons_add_group(s_break_glass_pin, 1, on_break_glass);
    xTaskCreate(hub_task, "hub", TASK_HUB_STACK, NULL, TASK_HUB_PRIO, NULL);
}

/* ================= Queries (any task) ================= */
bool hub_room_status(int room, room_status_t *out)
{
    if (room < 1 || room > ROOM_COUNT) {
        return false;
    }
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
