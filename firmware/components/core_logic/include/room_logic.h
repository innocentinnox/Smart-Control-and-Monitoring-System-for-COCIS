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
 */
#pragma once

#include "cocis_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t grace_ms;      /* no motion for this long -> vacant              */
    uint32_t warn_ms;       /* dim this long before switching off             */
    uint16_t dark_lux;      /* at or below: daylight is not enough            */
    uint16_t bright_lux;    /* at or above: daylight is enough, lamp off      */
    uint8_t  dim_level;     /* warning level, %                               */
    float    lamp_watts;    /* total lamp power of the room at 100 %          */
} room_logic_cfg_t;

typedef struct {
    bool     occupied;
    bool     daylight;          /* hysteresis memory: room bright enough */
    uint8_t  level;             /* current lamp level, %                 */
    uint32_t last_motion_ms;
    uint32_t last_step_ms;
    double   on_wh;             /* energy actually used                  */
    double   baseline_wh;       /* energy a manual room would have used  */
} room_logic_t;

typedef struct {
    bool         motion;
    uint16_t     lux;
    light_mode_t mode;
    bool         in_hours;
    uint32_t     now_ms;
} room_inputs_t;

/* What changed in this step, so the caller only reports real changes. */
#define ROOM_CHANGED_OCCUPANCY  (1u << 0)
#define ROOM_CHANGED_LEVEL      (1u << 1)

void     room_logic_init(room_logic_t *r, uint32_t now_ms);
unsigned room_logic_step(room_logic_t *r, const room_logic_cfg_t *cfg, const room_inputs_t *in);
double   room_logic_saved_wh(const room_logic_t *r);

#ifdef __cplusplus
}
#endif
