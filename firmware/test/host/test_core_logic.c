/* Host (PC) unit tests for the pure decision logic in components/core_logic.
 * No ESP32 needed: run ./run_tests.sh (uses a gcc Docker image).
 */
#include <stdio.h>
#include <string.h>

#include "access_policy.h"
#include "auth_fsm.h"
#include "room_logic.h"

static int s_failures, s_checks;

#define CHECK(cond)                                                           \
    do {                                                                      \
        s_checks++;                                                           \
        if (!(cond)) {                                                        \
            s_failures++;                                                     \
            printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);          \
        }                                                                     \
    } while (0)

#define HHMM(h, m) ((h) * 60 + (m))
enum { SUN, MON, TUE, WED, THU, FRI, SAT };

/* ---------------- Schedule ---------------- */
static void test_schedule(void)
{
    access_schedule_t day = { HHMM(7, 0), HHMM(22, 0), DAYS_MON_FRI };
    CHECK(schedule_is_open(&day, MON, HHMM(7, 0)));
    CHECK(schedule_is_open(&day, FRI, HHMM(21, 59)));
    CHECK(!schedule_is_open(&day, MON, HHMM(22, 0)));     /* close is exclusive */
    CHECK(!schedule_is_open(&day, MON, HHMM(6, 59)));
    CHECK(!schedule_is_open(&day, SUN, HHMM(12, 0)));     /* not a working day */
    CHECK(schedule_minutes_to_close(&day, MON, HHMM(21, 45)) == 15);
    CHECK(schedule_minutes_to_close(&day, MON, HHMM(23, 0)) == -1);

    /* Overnight window 22:00-06:00 starting Friday night only */
    access_schedule_t night = { HHMM(22, 0), HHMM(6, 0), 1u << FRI };
    CHECK(schedule_is_open(&night, FRI, HHMM(23, 0)));
    CHECK(schedule_is_open(&night, SAT, HHMM(5, 0)));     /* still Friday's window */
    CHECK(!schedule_is_open(&night, SAT, HHMM(23, 0)));
    CHECK(schedule_minutes_to_close(&night, FRI, HHMM(23, 0)) == 7 * 60);

    access_schedule_t bad = { HHMM(9, 0), HHMM(9, 0), DAYS_ALL };
    CHECK(!schedule_is_valid(&bad));
    CHECK(schedule_is_valid(&day));
}

/* ---------------- Access decisions ---------------- */
static access_reason_t decide(role_t role, door_side_t side, bool inside, bool open)
{
    access_request_t r = { .known = true, .enabled = true, .role = role,
                           .side = side, .inside = inside, .schedule_open = open };
    return access_decide(&r);
}

static void test_access(void)
{
    access_request_t unknown = { .known = false };
    CHECK(access_decide(&unknown) == ACCESS_UNKNOWN_CARD);

    access_request_t disabled = { .known = true, .enabled = false };
    CHECK(access_decide(&disabled) == ACCESS_DISABLED);

    /* Normal day */
    CHECK(decide(ROLE_STUDENT, SIDE_IN,  false, true) == ACCESS_OK);
    CHECK(decide(ROLE_STUDENT, SIDE_OUT, true,  true) == ACCESS_OK);

    /* Anti-passback, for every role */
    CHECK(decide(ROLE_STUDENT, SIDE_IN,  true,  true) == ACCESS_ANTI_PASSBACK);
    CHECK(decide(ROLE_ADMIN,   SIDE_OUT, false, true) == ACCESS_ANTI_PASSBACK);

    /* After hours: ordinary users cannot enter or leave, privileged can */
    CHECK(decide(ROLE_STAFF,    SIDE_IN,  false, false) == ACCESS_OUTSIDE_HOURS);
    CHECK(decide(ROLE_STUDENT,  SIDE_OUT, true,  false) == ACCESS_OUTSIDE_HOURS);
    CHECK(decide(ROLE_SECURITY, SIDE_IN,  false, false) == ACCESS_OK_PRIVILEGED);
    CHECK(decide(ROLE_ADMIN,    SIDE_OUT, true,  false) == ACCESS_OK_PRIVILEGED);

    /* Escort window lets people out, never in */
    access_request_t esc = { .known = true, .enabled = true, .role = ROLE_STUDENT,
                             .side = SIDE_OUT, .inside = true, .escort_active = true };
    CHECK(access_decide(&esc) == ACCESS_OK_ESCORT);
    esc.side = SIDE_IN; esc.inside = false;
    CHECK(access_decide(&esc) == ACCESS_OUTSIDE_HOURS);

    /* Lockdown beats opening hours for non-privileged users */
    access_request_t lock = { .known = true, .enabled = true, .role = ROLE_STAFF,
                              .side = SIDE_IN, .schedule_open = true, .lockdown = true };
    CHECK(access_decide(&lock) == ACCESS_LOCKDOWN);
    lock.role = ROLE_SECURITY;
    CHECK(access_decide(&lock) == ACCESS_OK_PRIVILEGED);

    CHECK(access_granted(ACCESS_OK_ESCORT) && !access_granted(ACCESS_ANTI_PASSBACK));
}

/* ---------------- Room lighting ---------------- */
static const room_logic_cfg_t ROOM_CFG = {
    .grace_ms = 30000, .warn_ms = 10000, .dark_lux = 150, .bright_lux = 400,
    .dim_level = 30, .lamp_watts = 360.0f,
};

static unsigned room_step(room_logic_t *r, bool motion, uint16_t lux, uint32_t t)
{
    room_inputs_t in = { .motion = motion, .lux = lux, .mode = LIGHT_AUTO,
                         .in_hours = true, .now_ms = t };
    return room_logic_step(r, &ROOM_CFG, &in);
}

static void test_room(void)
{
    room_logic_t r;
    room_logic_init(&r, 0);

    CHECK(room_step(&r, false, 50, 100) == 0 && r.level == 0);        /* empty, dark */
    unsigned ch = room_step(&r, true, 50, 1000);                         /* someone walks in */
    CHECK(ch == (ROOM_CHANGED_OCCUPANCY | ROOM_CHANGED_LEVEL));
    CHECK(r.occupied && r.level == 100);

    CHECK(room_step(&r, false, 50, 1000 + 19000) == 0 && r.level == 100); /* still in grace */
    room_step(&r, false, 50, 1000 + 21000);                              /* last 10 s: dim */
    CHECK(r.level == 30 && r.occupied);
    room_step(&r, true, 50, 1000 + 22000);                               /* wave: back on */
    CHECK(r.level == 100);
    room_step(&r, false, 50, 1000 + 22000 + 30000);                      /* grace over */
    CHECK(!r.occupied && r.level == 0);

    /* Daylight harvesting with hysteresis */
    room_step(&r, true, 500, 60000);
    CHECK(r.occupied && r.level == 0);           /* bright: no lamp needed */
    room_step(&r, true, 300, 61000);
    CHECK(r.level == 0);                         /* between thresholds: keep "daylight" */
    room_step(&r, true, 100, 62000);
    CHECK(r.level == 100);                       /* dark: lamp on */
    room_step(&r, true, 300, 63000);
    CHECK(r.level == 100);                       /* between thresholds: keep "dark" */

    /* Crossing a lux threshold is reported even when the lamp level does not change */
    room_logic_t dl;
    room_logic_init(&dl, 0);
    CHECK(room_step(&dl, false, 50, 100) == 0);                     /* vacant, dark: nothing */
    CHECK(room_step(&dl, false, 800, 200) == ROOM_CHANGED_DAYLIGHT); /* slider up: reported */
    CHECK(dl.daylight && dl.level == 0);

    /* Manual override */
    room_inputs_t off = { .motion = true, .lux = 50, .mode = LIGHT_OFF, .in_hours = true, .now_ms = 64000 };
    room_logic_step(&r, &ROOM_CFG, &off);
    CHECK(r.level == 0);

    /* Energy: one hour empty in opening hours saves the full lamp energy */
    room_logic_t e;
    room_logic_init(&e, 0);
    room_step(&e, false, 50, 3600000);
    CHECK(e.on_wh == 0.0 && e.baseline_wh > 359.9 && e.baseline_wh < 360.1);
    CHECK(room_logic_saved_wh(&e) > 359.9);
}

/* ---------------- Two-factor admin login ---------------- */
static const auth_cfg_t AUTH_CFG = {
    .step_timeout_ms = 30000, .session_ms = 300000, .max_failures = 3, .lockout_ms = 60000,
};

static void test_auth(void)
{
    auth_fsm_t a;
    auth_fsm_init(&a);

    /* Happy path */
    CHECK(auth_fsm_pin(&a, &AUTH_CFG, true, 0) == AUTH_NONE);      /* PIN before card: ignored */
    auth_fsm_arm(&a, &AUTH_CFG, 0);
    CHECK(auth_fsm_card(&a, &AUTH_CFG, 0xD4E5F6A7, true, 1000) == AUTH_CARD_OK);
    CHECK(auth_fsm_pin(&a, &AUTH_CFG, true, 2000) == AUTH_OK);
    CHECK(auth_fsm_in_session(&a, 2000));
    CHECK(auth_fsm_tick(&a, 2000 + 300000) == AUTH_TIMEOUT);         /* session expires */
    CHECK(!auth_fsm_in_session(&a, 2000 + 300000));

    /* Non-admin card is refused */
    auth_fsm_arm(&a, &AUTH_CFG, 0);
    CHECK(auth_fsm_card(&a, &AUTH_CFG, 0xA1B2C3D4, false, 10) == AUTH_NOT_ADMIN);
    CHECK(a.state == AUTH_ST_IDLE);

    /* Three wrong PINs lock the login for 60 s */
    auth_fsm_arm(&a, &AUTH_CFG, 0);
    auth_fsm_card(&a, &AUTH_CFG, 0xD4E5F6A7, true, 0);
    CHECK(auth_fsm_pin(&a, &AUTH_CFG, false, 100) == AUTH_BAD_PIN);
    CHECK(auth_fsm_pin(&a, &AUTH_CFG, false, 200) == AUTH_BAD_PIN);
    CHECK(auth_fsm_pin(&a, &AUTH_CFG, false, 300) == AUTH_LOCKED);
    CHECK(auth_fsm_arm(&a, &AUTH_CFG, 30000) == AUTH_LOCKED);
    CHECK(auth_fsm_arm(&a, &AUTH_CFG, 300 + 60000) == AUTH_NONE);    /* lockout over */

    /* A late PIN is a timeout even before the periodic tick runs */
    auth_fsm_arm(&a, &AUTH_CFG, 0);
    auth_fsm_card(&a, &AUTH_CFG, 0xD4E5F6A7, true, 1000);
    CHECK(auth_fsm_pin(&a, &AUTH_CFG, true, 1000 + 30000) == AUTH_TIMEOUT);
    CHECK(!auth_fsm_in_session(&a, 31000));

    /* Card step times out */
    auth_fsm_arm(&a, &AUTH_CFG, 0);
    CHECK(auth_fsm_tick(&a, 29999) == AUTH_NONE);
    CHECK(auth_fsm_tick(&a, 30000) == AUTH_TIMEOUT);

    /* Deadlines survive the 32-bit millisecond wrap-around */
    auth_fsm_arm(&a, &AUTH_CFG, 0xFFFFF000u);
    CHECK(auth_fsm_tick(&a, 0x00000010u) == AUTH_NONE);
}

/* ---------------- Enum helpers ---------------- */
static void test_types(void)
{
    role_t r;
    CHECK(role_parse("security", &r) && r == ROLE_SECURITY);
    CHECK(!role_parse("janitor", &r));
    CHECK(strcmp(access_reason_str(ACCESS_ANTI_PASSBACK), "ANTI_PASSBACK") == 0);
    CHECK(strcmp(log_type_str(LOG_ALERT), "ALERT") == 0);

    card_uid_t uid;
    CHECK(card_uid_parse("a1b2c3d4", &uid) && uid == 0xA1B2C3D4u);
    CHECK(!card_uid_parse("A1B2C3D4E", &uid));     /* too long */
    CHECK(!card_uid_parse("XYZ", &uid));
}

int main(void)
{
    struct { const char *name; void (*fn)(void); } suites[] = {
        { "schedule", test_schedule }, { "access", test_access },
        { "room",     test_room     }, { "auth",   test_auth   },
        { "types",    test_types    },
    };
    for (size_t i = 0; i < sizeof(suites) / sizeof(suites[0]); i++) {
        int before = s_failures;
        suites[i].fn();
        printf("%-9s %s\n", suites[i].name, s_failures == before ? "ok" : "FAILED");
    }
    printf("\n%d checks, %d failures\n", s_checks, s_failures);
    return s_failures ? 1 : 0;
}
