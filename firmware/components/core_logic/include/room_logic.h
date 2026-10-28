/* Lecture-room lighting brain (pure logic, no I/O).
 *
 * Inputs every sensing cycle: PIR motion, measured light level (lux), the
 * manual override mode and whether the building is in opening hours.
 * Output: the lamp level 0..100 %, plus occupancy and energy bookkeeping.
 *
 *  - Occupancy: motion marks the room occupied; no motion for grace_ms marks
 *    it vacant (PIRs only see movement, so a still audience needs the grace).
 *  - Pre-off warning: during the last warn_ms of the grace period the lamp
 *    dims to dim_level, so people still in the room wave and keep the light on.
 *  - Daylight harvesting with hysteresis: lights stay off when the room is
 *    bright (>= bright_lux), come back when it gets dark (<= dark_lux).
 *    The gap stops flicker when the reading hovers around one threshold.
 *  - Energy: integrates actual consumption and a baseline of "lamp fully on
 *    for all opening hours" (what happens with no automation); the difference
 *    is the energy saved.
 *
 * Each room node task owns one room_logic_t and calls room_logic_step() on
 * every sensing cycle (about 5 Hz); the node does the actual sensor reads and
 * PWM output. No hardware here, so it is unit tested on the PC.
 */
#pragma once

#include "cocis_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Tuning for one room, supplied by the room node. */
typedef struct {
    uint32_t grace_ms;      /* no motion for this long -> vacant              */
    uint32_t warn_ms;       /* dim this long before switching off             */
    uint16_t dark_lux;      /* at or below: daylight is not enough            */
    uint16_t bright_lux;    /* at or above: daylight is enough, lamp off      */
    uint8_t  dim_level;     /* warning level, %                               */
    float    lamp_watts;    /* total lamp power of the room at 100 %          */
} room_logic_cfg_t;

/* State carried from one step to the next. Initialise with room_logic_init(). */
typedef struct {
    /* someone is (believed to be) in the room */
    bool     occupied;
    bool     daylight;          /* hysteresis memory: room bright enough */
    uint8_t  level;             /* current lamp level, %                 */
    /* time of the most recent PIR motion, ms */
    uint32_t last_motion_ms;
    /* time of the previous room_logic_step(), ms (for energy integration) */
    uint32_t last_step_ms;
    double   on_wh;             /* energy actually used                  */
    double   baseline_wh;       /* energy a manual room would have used  */
} room_logic_t;

/* One sensing cycle's readings, filled in by the room node. */
typedef struct {
    /* PIR currently reports movement */
    bool         motion;
    /* measured light level, lux (from the LDR) */
    uint16_t     lux;
    /* override from the hub: AUTO, or forced ON / OFF */
    light_mode_t mode;
    /* building is within opening hours (counts towards the baseline) */
    bool         in_hours;
    /* current time, ms (free-running counter, may wrap) */
    uint32_t     now_ms;
} room_inputs_t;

/* What changed in this step, so the caller only reports real changes.
 * Bit flags: room_logic_step() ORs together the ones that apply, and the
 * caller tests them with "&", e.g. if (changed & ROOM_CHANGED_LEVEL). */
#define ROOM_CHANGED_OCCUPANCY  (1u << 0)
#define ROOM_CHANGED_LEVEL      (1u << 1)
#define ROOM_CHANGED_DAYLIGHT   (1u << 2)   /* crossed a lux threshold */

/* Start fresh: vacant, lamp off, no daylight, energy counters zero.
 * @param r       the room state to clear
 * @param now_ms  current time, ms (starting point for energy integration) */
void     room_logic_init(room_logic_t *r, uint32_t now_ms);

/* Run one sensing cycle: update energy, occupancy and daylight, then pick
 * the lamp level (100 / 0 when forced ON / OFF, automatic in AUTO).
 * @param r    room state, updated in place (r->level is the new lamp %)
 * @param cfg  room tuning
 * @param in   this cycle's readings
 * @return ROOM_CHANGED_* bits for what changed since the previous step,
 *         0 if nothing did */
unsigned room_logic_step(room_logic_t *r, const room_logic_cfg_t *cfg, const room_inputs_t *in);

/* Energy saved so far compared with the "always on in opening hours" room.
 * @param r  room state
 * @return baseline_wh - on_wh in watt-hours, never negative (0 if the
 *         room has used more than the baseline, e.g. forced ON at night) */
double   room_logic_saved_wh(const room_logic_t *r);

#ifdef __cplusplus
}
#endif
