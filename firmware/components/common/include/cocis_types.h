/* Shared vocabulary of the whole system (C mirror of docs/event-schema.md).
 *
 * Pure C on purpose: no ESP-IDF headers, so the decision logic that uses
 * these types can also be compiled and unit-tested on a PC.
 *
 * Every enum is declared once through an X-macro list. The same list then
 * generates the enum values AND the matching string table, so a name can
 * never drift out of sync with its value.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------- Identities ---------------- */
typedef uint32_t card_uid_t;            /* printed as 8 hex digits, e.g. A1B2C3D4 */
#define CARD_UID_NONE   0u
#define USER_NAME_MAX   16              /* including the terminating NUL */

typedef enum {
    NODE_HUB = 0,                       /* main entrance terminal (the authority) */
    NODE_ROOM1,
    NODE_ROOM2,
    NODE_COUNT
} node_id_t;

#define ROOM_COUNT 2                    /* rooms are numbered 1..ROOM_COUNT */
static inline node_id_t room_to_node(int room) { return (node_id_t)(NODE_HUB + room); }
static inline int       node_to_room(node_id_t n) { return (int)n - (int)NODE_HUB; }

/* ---------------- X-macro enum lists ---------------- */
#define COCIS_ROLES(X)   X(STUDENT) X(STAFF) X(SECURITY) X(ADMIN)
#define COCIS_SIDES(X)   X(IN) X(OUT)
#define COCIS_LIGHT_MODES(X) X(AUTO) X(ON) X(OFF)

/* Order matters: every reason up to ACCESS_OK_ESCORT means "granted". */
#define COCIS_ACCESS_REASONS(X) \
    X(OK) X(OK_PRIVILEGED) X(OK_ESCORT) \
    X(UNKNOWN_CARD) X(DISABLED) X(OUTSIDE_HOURS) X(ANTI_PASSBACK) X(LOCKDOWN)

#define COCIS_LOG_TYPES(X) \
    X(BOOT) X(ACCESS) X(ROOM) X(LIGHT) X(ENERGY) X(CONFIG) X(USER) X(AUTH) X(ALERT)

#define COCIS_AUTH_RESULTS(X) \
    X(NONE) X(CARD_OK) X(OK) X(BAD_PIN) X(LOCKED) X(NOT_ADMIN) X(TIMEOUT) X(LOGOUT)

#define COCIS_ENUM_ROLE(n)    ROLE_##n,
#define COCIS_ENUM_SIDE(n)    SIDE_##n,
#define COCIS_ENUM_LIGHT(n)   LIGHT_##n,
#define COCIS_ENUM_ACCESS(n)  ACCESS_##n,
#define COCIS_ENUM_LOG(n)     LOG_##n,
#define COCIS_ENUM_AUTH(n)    AUTH_##n,

typedef enum { COCIS_ROLES(COCIS_ENUM_ROLE)            ROLE_COUNT   } role_t;
typedef enum { COCIS_SIDES(COCIS_ENUM_SIDE)            SIDE_COUNT   } door_side_t;
typedef enum { COCIS_LIGHT_MODES(COCIS_ENUM_LIGHT)     LIGHT_MODE_COUNT } light_mode_t;
typedef enum { COCIS_ACCESS_REASONS(COCIS_ENUM_ACCESS) ACCESS_COUNT } access_reason_t;
typedef enum { COCIS_LOG_TYPES(COCIS_ENUM_LOG)         LOG_TYPE_COUNT } log_type_t;
typedef enum { COCIS_AUTH_RESULTS(COCIS_ENUM_AUTH)     AUTH_RESULT_COUNT } auth_result_t;

/* ---------------- Enum <-> string (cocis_types.c) ---------------- */
const char *role_str(role_t v);
const char *side_str(door_side_t v);
const char *light_mode_str(light_mode_t v);
const char *access_reason_str(access_reason_t v);
const char *log_type_str(log_type_t v);
const char *auth_result_str(auth_result_t v);

/* Case-insensitive parsers; return false if the text matches no value. */
bool role_parse(const char *s, role_t *out);
bool side_parse(const char *s, door_side_t *out);
bool light_mode_parse(const char *s, light_mode_t *out);

/* ---------------- Small helpers ---------------- */
static inline bool role_is_privileged(role_t r) { return r == ROLE_SECURITY || r == ROLE_ADMIN; }
static inline bool access_granted(access_reason_t r) { return r <= ACCESS_OK_ESCORT; }

/* Deadline check on a millisecond counter. The signed difference keeps it
 * correct across the 32-bit wrap-around (every ~49.7 days). */
static inline bool time_reached_ms(uint32_t now_ms, uint32_t deadline_ms)
{
    return (int32_t)(now_ms - deadline_ms) >= 0;
}

/* "A1B2C3D4" <-> 0xA1B2C3D4. Parse accepts 1..8 hex digits. */
bool card_uid_parse(const char *s, card_uid_t *out);

#ifdef __cplusplus
}
#endif
