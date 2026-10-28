/* Lecture-room lighting logic (see room_logic.h for the behaviour).
 * Pure logic: called by the room node task, unit tested on the PC. */
#include "room_logic.h"

#include <string.h>

#define MS_PER_HOUR 3600000.0       /* 60 * 60 * 1000; a double so the division below is not integer */

void room_logic_init(room_logic_t *r, uint32_t now_ms)
{
    memset(r, 0, sizeof(*r));
    r->last_step_ms = now_ms;
}

/* Lamp level the room should have right now (AUTO mode only).
 *   vacant or bright enough       -> 0 %
 *   occupied, in the warning time -> dim_level %
 *   occupied otherwise            -> 100 % */
static uint8_t auto_level(const room_logic_t *r, const room_logic_cfg_t *cfg, uint32_t now_ms)
{
    if (!r->occupied || r->daylight) {
        return 0;
    }
    uint32_t idle = now_ms - r->last_motion_ms;           /* wrap-safe unsigned maths */
    /* "idle + warn >= grace" is "idle >= grace - warn" (the last warn_ms of
     * the grace period), written without a subtraction that could go below
     * zero if warn_ms > grace_ms. warn_ms == 0 turns the warning off. */
    bool warning  = cfg->warn_ms && idle + cfg->warn_ms >= cfg->grace_ms;
    return warning ? cfg->dim_level : 100;
}

/* Motion -> occupied now (and restart the grace timer).
 * No motion for grace_ms -> vacant. In between nothing changes. */
static void update_occupancy(room_logic_t *r, const room_logic_cfg_t *cfg, const room_inputs_t *in)
{
    if (in->motion) {
        r->occupied       = true;
        r->last_motion_ms = in->now_ms;
    } else if (r->occupied && in->now_ms - r->last_motion_ms >= cfg->grace_ms) {
        /* unsigned subtraction: elapsed time stays correct across the ms counter wrap */
        r->occupied = false;
    }
}

/* Hysteresis: two thresholds instead of one. Above bright_lux -> daylight,
 * below dark_lux -> no daylight, and between them the previous answer is
 * kept, so a reading wobbling around one value cannot toggle the lamp. */
static void update_daylight(room_logic_t *r, const room_logic_cfg_t *cfg, uint16_t lux)
{
    if (lux >= cfg->bright_lux) {
        r->daylight = true;
    } else if (lux <= cfg->dark_lux) {
        r->daylight = false;
    }                                       /* in between: keep the last decision */
}

/* Integrate energy over the time since the previous step, at the old level.
 * energy (Wh) = power (W) * time (h). The lamp was at r->level for the whole
 * interval that just ended, so this must run BEFORE the level is updated.
 * The baseline assumes a lamp at 100 % throughout opening hours. */
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
    /* snapshot the old values so we can report what changed */
    unsigned changed = 0;
    bool was_occupied = r->occupied;
    bool was_daylight = r->daylight;
    uint8_t old_level = r->level;

    /* energy first: it must use the level from the previous step */
    update_energy(r, cfg, in);
    update_occupancy(r, cfg, in);
    update_daylight(r, cfg, in->lux);

    /* Forced modes ignore occupancy and daylight, but those are still
     * tracked above so AUTO resumes from up-to-date values. */
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
    /* clamp at 0: forcing the lamp on outside hours can use more than the baseline */
    double saved = r->baseline_wh - r->on_wh;
    return saved > 0 ? saved : 0;
}
