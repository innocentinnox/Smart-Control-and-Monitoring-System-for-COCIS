/* String tables for the enums in cocis_types.h, generated from the same
 * X-macro lists so names and values can never disagree. */
#include "cocis_types.h"

#include <ctype.h>
#include <stdlib.h>

#define COCIS_STR(n) #n,

static const char *const s_roles[]   = { COCIS_ROLES(COCIS_STR) };
static const char *const s_sides[]   = { COCIS_SIDES(COCIS_STR) };
static const char *const s_lights[]  = { COCIS_LIGHT_MODES(COCIS_STR) };
static const char *const s_reasons[] = { COCIS_ACCESS_REASONS(COCIS_STR) };
static const char *const s_logs[]    = { COCIS_LOG_TYPES(COCIS_STR) };
static const char *const s_auth[]    = { COCIS_AUTH_RESULTS(COCIS_STR) };

/* ---------------- Generic table helpers ---------------- */
static const char *table_str(const char *const *table, size_t count, int v)
{
    return (v >= 0 && (size_t)v < count) ? table[v] : "?";
}

static bool str_ieq(const char *a, const char *b)
{
    while (*a && *b) {
        if (toupper((unsigned char)*a++) != toupper((unsigned char)*b++)) {
            return false;
        }
    }
    return *a == *b;
}

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

#define COUNT_OF(a) (sizeof(a) / sizeof((a)[0]))

/* ---------------- enum -> string ---------------- */
const char *role_str(role_t v)                { return table_str(s_roles,   COUNT_OF(s_roles),   v); }
const char *side_str(door_side_t v)           { return table_str(s_sides,   COUNT_OF(s_sides),   v); }
const char *light_mode_str(light_mode_t v)    { return table_str(s_lights,  COUNT_OF(s_lights),  v); }
const char *access_reason_str(access_reason_t v) { return table_str(s_reasons, COUNT_OF(s_reasons), v); }
const char *log_type_str(log_type_t v)        { return table_str(s_logs,    COUNT_OF(s_logs),    v); }
const char *auth_result_str(auth_result_t v)  { return table_str(s_auth,    COUNT_OF(s_auth),    v); }

/* ---------------- string -> enum ---------------- */
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
    size_t len = 0;
    for (const char *p = s; *p; p++, len++) {
        if (!isxdigit((unsigned char)*p) || len >= 8) {
            return false;
        }
    }
    *out = (card_uid_t)strtoul(s, NULL, 16);
    return *out != CARD_UID_NONE;
}
