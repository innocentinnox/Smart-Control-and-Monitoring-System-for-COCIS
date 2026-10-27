/* Entrance door actuators (servo locks): entry door and exit door.
 * door_open() swings the door open and an esp_timer closes it again, so the
 * caller never waits. Safe to call from any task. */
#pragma once

#include "cocis_types.h"

void door_init(void);
void door_open(door_side_t side, uint32_t ms);

/* Emergency: hold both doors open until released. */
void door_hold_all(bool hold);
bool door_is_held(void);
