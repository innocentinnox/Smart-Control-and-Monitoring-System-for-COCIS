/* Lecture-room terminal: PIR + LDR in, dimmable lights out.
 *
 * Each room runs its own task and makes its own lighting decisions with
 * room_logic, so lights keep working even if the hub is down. It reports
 * to the hub over the bus (state on change + heartbeat) and accepts manual
 * overrides (MSG_LIGHT_CMD) from it.
 *
 * In this build both rooms are tasks on the same ESP32-S3 as the hub, but
 * they share no variables with it: everything goes through the bus, exactly
 * as it would between separate boards.
 */
#pragma once

#include <stdbool.h>

/* Tells the room whether the building is in opening hours (for the energy
 * baseline). On separate boards this comes from the hub's periodic sync.
 * Called from the room task every sensing period, so it must be quick. */
typedef bool (*room_hours_fn)(void);

/* Set up one room's PIR input, lamp PWM and LDR, then start its task.
 * Call once per room at boot, after bus_init() and light_sensor_init().
 * @param room      room number, 1..ROOM_COUNT
 * @param in_hours  callback: is the building in opening hours right now? */
void room_node_start(int room, room_hours_fn in_hours);   /* room = 1..ROOM_COUNT */

/* Demo aid: make a room stop talking to the hub (node failure test).
 * While the link is down the room keeps running its lights but sends
 * nothing, so after CONFIG_COCIS_NODE_TIMEOUT_S the hub marks it offline.
 * @param room  room number, 1..ROOM_COUNT (others are ignored)
 * @param up    false = go silent, true = talk to the hub again
 * Called from the CLI ("sim" command). */
void room_node_set_link(int room, bool up);
