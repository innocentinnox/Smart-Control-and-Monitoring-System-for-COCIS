/* Entrance door actuators (servo locks): entry door and exit door.
 *
 * Each door is one SG90-style servo: 0 degrees = closed/locked, 90 degrees =
 * open. door_open() swings the door open and arms a one-shot esp_timer that
 * closes it again later, so the caller (the hub task) never has to wait or
 * remember to close it. Safe to call from any task.
 *
 * Break-glass emergency: door_hold_all(true) opens both doors and keeps them
 * open (the auto-close timers are ignored) until door_hold_all(false).
 */
#pragma once

#include "cocis_types.h"

/* Set up both servos (PWM channels) and create their auto-close timers.
 * Call once at boot, before any door_open(). Called from hub_start(). */
void door_init(void);

/* Open one door now and close it automatically after `ms` milliseconds.
 * Opening a door that is already open restarts its countdown, so two quick
 * grants keep it open for the full time after the second one.
 * @param side  SIDE_IN (entry door) or SIDE_OUT (exit door)
 * @param ms    how long the door stays open, in milliseconds
 *              (the hub uses CONFIG_COCIS_DOOR_OPEN_MS, default 3000) */
void door_open(door_side_t side, uint32_t ms);

/* Emergency: hold both doors open until released.
 * @param hold  true  = open both doors and keep them open (timers ignored)
 *              false = release the hold and close both doors now */
void door_hold_all(bool hold);

/* @return true while door_hold_all(true) is in effect (emergency active). */
bool door_is_held(void);
