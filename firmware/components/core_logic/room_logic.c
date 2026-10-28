#include "room_logic.h"

#include <string.h>

#define MS_PER_HOUR 3600000.0

void room_logic_init(room_logic_t *r, uint32_t now_ms)
{
    memset(r, 0, sizeof(*r));
    r->last_step_ms = now_ms;
}

/* Lamp level the room should have right now (AUTO mode only). */
static uint8_t auto_level(const room_logic_t *r, const room_logic_cfg_t *cfg, uint32_t now_ms)
{
    if (!r->occupied || r->daylight) {
        return 0;
    }
    uint32_t idle = now_ms - r->last_motion_ms;           /* wrap-safe unsigned maths */
    bool warning  = cfg->warn_ms && idle + cfg->warn_ms >= cfg->grace_ms;
    return warning ? cfg->dim_level : 100;
}

static void update_occupancy(room_logic_t *r, const room_logic_cfg_t *cfg, const room_inputs_t *in)
{
    if (in->motion) {
        r->occupied       = true;
        r->last_motion_ms = in->now_ms;
    } else if (r->occupied && in->now_ms - r->last_motion_ms >= cfg->grace_ms) {
        r->occupied = false;
    }
}

static void update_daylight(room_logic_t *r, const room_logic_cfg_t *cfg, uint16_t lux)
{
    if (lux >= cfg->bright_lux) {
        r->daylight = true;
    } else if (lux <= cfg->dark_lux) {
        r->daylight = false;
    }                                       /* in between: keep the last decision */
}

/* Integrate energy over the time since the previous step, at the old level. */
static void update_energy(room_logic_t *r, const room_logic_cfg_t *cfg, const room_inputs_t *in)
{
    double hours = (double)(uint32_t)(in->now_ms - r->last_step_ms) / MS_PER_HOUR;
    r->on_wh += cfg->lamp_watts * (r->level / 100.0) * hours;
    if (in->in_hours) {
        r->baseline_wh += cfg->lamp_watts * hours;
    }
    r->last_step_ms = in->now_ms;
}

unsigned room_logic_step(room_logic_t *r, const room_logic_cfg_t *cfg, const room_inputs_t *in)
{
    unsigned changed = 0;
    bool was_occupied = r->occupied;
    bool was_daylight = r->daylight;
    uint8_t old_level = r->level;

    update_energy(r, cfg, in);
    update_occupancy(r, cfg, in);
    update_daylight(r, cfg, in->lux);

    switch (in->mode) {
    case LIGHT_ON:  r->level = 100; break;
    case LIGHT_OFF: r->level = 0;   break;
    default:        r->level = auto_level(r, cfg, in->now_ms); break;
    }

    if (r->occupied != was_occupied) {
        changed |= ROOM_CHANGED_OCCUPANCY;
    }
    if (r->level != old_level) {
        changed |= ROOM_CHANGED_LEVEL;
    }
    if (r->daylight != was_daylight) {
        changed |= ROOM_CHANGED_DAYLIGHT;
    }
    return changed;
}

double room_logic_saved_wh(const room_logic_t *r)
{
    double saved = r->baseline_wh - r->on_wh;
    return saved > 0 ? saved : 0;
}
