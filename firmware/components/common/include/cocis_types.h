/* Shared vocabulary of the whole system (C mirror of docs/event-schema.md).
 *
 * Pure C on purpose: no ESP-IDF headers, so the decision logic that uses
 * these types can also be compiled and unit-tested on a PC.
 *
 * Every enum is declared once through an X-macro list. The same list then
 * generates the enum values AND the matching string table, so a name can
 * never drift out of sync with its value.
 *
 * How an X-macro works (example with COCIS_SIDES):
 *   #define COCIS_SIDES(X)  X(IN) X(OUT)
 * The list takes "X" as a parameter, i.e. "what to do with each item".
 *   COCIS_SIDES(COCIS_ENUM_SIDE)  expands to  SIDE_IN, SIDE_OUT,
 *   COCIS_SIDES(COCIS_STR)        expands to  "IN", "OUT",     (cocis_types.c)
 * Both come from the same list, so item n of the enum is always item n of
 * the string table. To add a value, add it to the list only.
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
#define CARD_UID_NONE   0u              /* "no card"; never a valid UID */
#define USER_NAME_MAX   16              /* including the terminating NUL */

/* The three terminals. Also used as array index (bus inboxes, seq tables),
 * so the values must stay 0..NODE_COUNT-1. */
typedef enum {
    NODE_HUB = 0,                       /* main entrance terminal (the authority) */
    NODE_ROOM1,
    NODE_ROOM2,
    NODE_COUNT
} node_id_t;

#define ROOM_COUNT 2                    /* rooms are numbered 1..ROOM_COUNT */

/* Room number (1..ROOM_COUNT) -> its node id, e.g. 1 -> NODE_ROOM1.
 * Works because the room nodes directly follow NODE_HUB in the enum.
 * No range check: the caller must pass a valid room number. */
static inline node_id_t room_to_node(int room) { return (node_id_t)(NODE_HUB + room); }

/* Node id -> room number (the inverse of room_to_node). NODE_HUB gives 0. */
static inline int       node_to_room(node_id_t n) { return (int)n - (int)NODE_HUB; }

/* ---------------- X-macro enum lists ---------------- */
/* User roles; SECURITY and ADMIN are "privileged" (see role_is_privileged). */
#define COCIS_ROLES(X)   X(STUDENT) X(STAFF) X(SECURITY) X(ADMIN)
/* Door side / card reader: IN = entering the building, OUT = leaving it. */
#define COCIS_SIDES(X)   X(IN) X(OUT)
/* Room light override: AUTO = room_logic decides, ON / OFF = forced. */
#define COCIS_LIGHT_MODES(X) X(AUTO) X(ON) X(OFF)

/* Result of access_decide(): why a card was let through or refused.
 * Order matters: every reason up to ACCESS_OK_ESCORT means "granted". */
#define COCIS_ACCESS_REASONS(X) \
    X(OK) X(OK_PRIVILEGED) X(OK_ESCORT) \
    X(UNKNOWN_CARD) X(DISABLED) X(OUTSIDE_HOURS) X(ANTI_PASSBACK) X(LOCKDOWN)

/* Event categories; the name is written as the TYPE field of a log line. */
#define COCIS_LOG_TYPES(X) \
    X(BOOT) X(ACCESS) X(ROOM) X(LIGHT) X(ENERGY) X(CONFIG) X(USER) X(AUTH) X(ALERT)

/* What happened on one step of the admin login state machine (auth_fsm.h).
 * NONE = nothing to report. */
#define COCIS_AUTH_RESULTS(X) \
    X(NONE) X(CARD_OK) X(OK) X(BAD_PIN) X(LOCKED) X(NOT_ADMIN) X(TIMEOUT) X(LOGOUT)

/* "Item -> enum constant" generators: ## glues the prefix to the item name,
 * e.g. COCIS_ENUM_ROLE(ADMIN) -> ROLE_ADMIN, */
#define COCIS_ENUM_ROLE(n)    ROLE_##n,
#define COCIS_ENUM_SIDE(n)    SIDE_##n,
#define COCIS_ENUM_LIGHT(n)   LIGHT_##n,
#define COCIS_ENUM_ACCESS(n)  ACCESS_##n,
#define COCIS_ENUM_LOG(n)     LOG_##n,
#define COCIS_ENUM_AUTH(n)    AUTH_##n,

/* The enums themselves. Values count up from 0 in list order; the trailing
 * *_COUNT entry is therefore the number of items (handy for range checks). */
typedef enum { COCIS_ROLES(COCIS_ENUM_ROLE)            ROLE_COUNT   } role_t;
typedef enum { COCIS_SIDES(COCIS_ENUM_SIDE)            SIDE_COUNT   } door_side_t;
typedef enum { COCIS_LIGHT_MODES(COCIS_ENUM_LIGHT)     LIGHT_MODE_COUNT } light_mode_t;
typedef enum { COCIS_ACCESS_REASONS(COCIS_ENUM_ACCESS) ACCESS_COUNT } access_reason_t;
typedef enum { COCIS_LOG_TYPES(COCIS_ENUM_LOG)         LOG_TYPE_COUNT } log_type_t;
typedef enum { COCIS_AUTH_RESULTS(COCIS_ENUM_AUTH)     AUTH_RESULT_COUNT } auth_result_t;

/* ---------------- Enum <-> string (cocis_types.c) ----------------
 * Each returns the upper-case name without the prefix, e.g.
 * role_str(ROLE_ADMIN) -> "ADMIN". An out-of-range value returns "?"
 * (never NULL), so the result can always be passed to printf.
 * The strings are constant: do not modify or free them. Call from anywhere. */
const char *role_str(role_t v);
const char *side_str(door_side_t v);
const char *light_mode_str(light_mode_t v);
const char *access_reason_str(access_reason_t v);
const char *log_type_str(log_type_t v);
const char *auth_result_str(auth_result_t v);

/* Case-insensitive parsers; return false if the text matches no value.
 * @param s    text such as "admin" or "OUT" (NULL is allowed: returns false)
 * @param out  receives the value; left untouched when false is returned
 * @return true if s exactly matches one name (ignoring case) */
bool role_parse(const char *s, role_t *out);
bool side_parse(const char *s, door_side_t *out);
bool light_mode_parse(const char *s, light_mode_t *out);

/* ---------------- Small helpers ---------------- */
/* Security and admin are "24/7" roles: they pass outside hours and in lockdown. */
static inline bool role_is_privileged(role_t r) { return r == ROLE_SECURITY || r == ROLE_ADMIN; }
/* Turns a verdict into yes/no, relying on the "granted reasons come first" order above. */
static inline bool access_granted(access_reason_t r) { return r <= ACCESS_OK_ESCORT; }

/* Deadline check on a millisecond counter. The signed difference keeps it
 * correct across the 32-bit wrap-around (every ~49.7 days).
 *
 * Why it works: a uint32_t ms counter rolls over from 0xFFFFFFFF to 0.
 * A plain "now >= deadline" would then be wrong (a deadline just after the
 * wrap looks smaller than "now" just before it). now - deadline in unsigned
 * maths wraps the same way the counter does, and reading the result as
 * signed gives "how far past the deadline" (negative = not yet), as long as
 * the two times are less than ~24.8 days (2^31 ms) apart.
 *
 * @param now_ms       current time, ms (any free-running counter)
 * @param deadline_ms  the moment to test against, same counter
 * @return true once now_ms is at or after deadline_ms
 */
static inline bool time_reached_ms(uint32_t now_ms, uint32_t deadline_ms)
{
    return (int32_t)(now_ms - deadline_ms) >= 0;
}

/* Parses a card UID written in hex: "A1B2C3D4" -> 0xA1B2C3D4.
 * Parse accepts 1..8 hex digits (upper or lower case, no "0x" prefix).
 * @param s    text to parse; NULL or empty returns false
 * @param out  receives the UID
 * @return false if s has a non-hex character, more than 8 digits, or is
 *         zero (CARD_UID_NONE is not a valid card). Note that *out is still
 *         written (with 0) in the zero case. */
bool card_uid_parse(const char *s, card_uid_t *out);

#ifdef __cplusplus
}
#endif
