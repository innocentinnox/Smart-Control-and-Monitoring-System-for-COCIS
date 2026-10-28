/* String tables for the enums in cocis_types.h, generated from the same
 * X-macro lists so names and values can never disagree.
 *
 * Pure C (no ESP-IDF), so it is compiled into the host unit tests too.
 */
#include "cocis_types.h"

#include <ctype.h>
#include <stdlib.h>

/* "Item -> string literal" generator: # turns the macro argument into a
 * string, e.g. COCIS_STR(ADMIN) -> "ADMIN", */
#define COCIS_STR(n) #n,

/* One table per enum; index = enum value (both come from the same list). */
static const char *const s_roles[]   = { COCIS_ROLES(COCIS_STR) };
static const char *const s_sides[]   = { COCIS_SIDES(COCIS_STR) };
static const char *const s_lights[]  = { COCIS_LIGHT_MODES(COCIS_STR) };
static const char *const s_reasons[] = { COCIS_ACCESS_REASONS(COCIS_STR) };
static const char *const s_logs[]    = { COCIS_LOG_TYPES(COCIS_STR) };
static const char *const s_auth[]    = { COCIS_AUTH_RESULTS(COCIS_STR) };

/* ---------------- Generic table helpers ---------------- */

/* table[v], or "?" when v is outside 0..count-1 (e.g. a corrupted value),
 * so callers never index past the end of the array. */
static const char *table_str(const char *const *table, size_t count, int v)
{
    return (v >= 0 && (size_t)v < count) ? table[v] : "?";
}

/* Case-insensitive string equality, a portable version of strcasecmp() == 0
 * (strcasecmp is POSIX, not standard C). The cast to unsigned char is
 * required: toupper() is undefined for negative char values. */
static bool str_ieq(const char *a, const char *b)
{
    while (*a && *b) {
        if (toupper((unsigned char)*a++) != toupper((unsigned char)*b++)) {
            return false;
        }
    }
    /* equal only if both strings ended together (no prefix matches) */
    return *a == *b;
}

/* Linear search of a name table. Writes the matching index to *out and
 * returns true; returns false (and leaves *out alone) if nothing matches
 * or s is NULL. The tables are tiny, so a linear scan is fine. */
static bool table_parse(const char *const *table, size_t count, const char *s, int *out)
{
    for (size_t i = 0; s && i < count; i++) {
        if (str_ieq(s, table[i])) {
            *out = (int)i;
            return true;
        }
    }
    return false;
}

/* Number of elements in a real array (not a pointer). */
#define COUNT_OF(a) (sizeof(a) / sizeof((a)[0]))

/* ---------------- enum -> string ---------------- */
const char *role_str(role_t v)                { return table_str(s_roles,   COUNT_OF(s_roles),   v); }
const char *side_str(door_side_t v)           { return table_str(s_sides,   COUNT_OF(s_sides),   v); }
const char *light_mode_str(light_mode_t v)    { return table_str(s_lights,  COUNT_OF(s_lights),  v); }
const char *access_reason_str(access_reason_t v) { return table_str(s_reasons, COUNT_OF(s_reasons), v); }
const char *log_type_str(log_type_t v)        { return table_str(s_logs,    COUNT_OF(s_logs),    v); }
const char *auth_result_str(auth_result_t v)  { return table_str(s_auth,    COUNT_OF(s_auth),    v); }

/* ---------------- string -> enum ----------------
 * DEFINE_PARSER writes one complete parser function, so the three parsers
 * below are identical apart from their name, type and table.
 * table_parse() reports an int index, so it goes into a local int first and
 * is then converted to the enum type; *out is written only on success. */
#define DEFINE_PARSER(fn, type, table)                         \
    bool fn(const char *s, type *out)                          \
    {                                                          \
        int v;                                                 \
        if (!table_parse(table, COUNT_OF(table), s, &v)) {     \
            return false;                                      \
        }                                                      \
        *out = (type)v;                                        \
        return true;                                           \
    }

DEFINE_PARSER(role_parse,       role_t,       s_roles)
DEFINE_PARSER(side_parse,       door_side_t,  s_sides)
DEFINE_PARSER(light_mode_parse, light_mode_t, s_lights)

/* ---------------- Card UIDs ---------------- */
bool card_uid_parse(const char *s, card_uid_t *out)
{
    if (!s || !*s) {
        return false;
    }
    /* Validate first: strtoul alone would silently stop at the first bad
     * character ("12XY" -> 0x12) or accept a "0x" prefix / leading spaces.
     * More than 8 digits would not fit in 32 bits. */
    size_t len = 0;
    for (const char *p = s; *p; p++, len++) {
        if (!isxdigit((unsigned char)*p) || len >= 8) {
            return false;
        }
    }
    *out = (card_uid_t)strtoul(s, NULL, 16);
    /* 0 is reserved for "no card", so it is rejected even though it parsed */
    return *out != CARD_UID_NONE;
}
