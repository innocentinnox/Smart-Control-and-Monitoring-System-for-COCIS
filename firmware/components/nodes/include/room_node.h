/* Lecture-room terminal: PIR + LDR in, dimmable lights out.
 *
 * Each room runs its own task and makes its own lighting decisions with
 * room_logic, so lights keep working even if the hub is down. It reports
 * to the hub over the bus (state on change + heartbeat) and accepts manual
 * overrides (MSG_LIGHT_CMD) from it.
 */
#pragma once

#include <stdbool.h>

/* Tells the room whether the building is in opening hours (for the energy
 * baseline). On separate boards this comes from the hub's periodic sync. */
typedef bool (*room_hours_fn)(void);

void room_node_start(int room, room_hours_fn in_hours);   /* room = 1..ROOM_COUNT */

/* Demo aid: make a room stop talking to the hub (node failure test). */
void room_node_set_link(int room, bool up);
