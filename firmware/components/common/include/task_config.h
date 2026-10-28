/* Every FreeRTOS task in the system: name, stack (bytes) and priority in
 * one table, so the scheduling design can be read (and defended) at a glance.
 *
 * FreeRTOS basics: a "task" is an independent thread with its own stack.
 * The scheduler always runs the highest-priority task that is ready; a task
 * that is waiting (on a queue, a delay, a key press...) uses no CPU, so lower
 * priorities still get time whenever the urgent ones are blocked.
 *
 * Stack sizes are in BYTES (in ESP-IDF xTaskCreate() takes bytes, unlike
 * vanilla FreeRTOS which counts words). They are sized for the deepest call
 * chain of each task plus margin; printf, FATFS and SHA-256 are the big users.
 *
 * Priority rationale (higher number = more urgent):
 *   6  input tasks (keypad, buttons): short bursts, must not miss presses
 *   5  hub: the single authority that makes access decisions
 *   4  room nodes: 5 Hz sensing loop, not time-critical
 *   3  storage gatekeeper: SD writes can be slow, must never block the hub
 *   2  ui: LCD + buzzer, purely cosmetic
 *   1  serial console (REPL): runs whenever nothing else needs the CPU
 */
#pragma once

#define TASK_KEYPAD_STACK   3072
#define TASK_KEYPAD_PRIO    6

#define TASK_BUTTONS_STACK  3072
#define TASK_BUTTONS_PRIO   6

#define TASK_HUB_STACK      6144
#define TASK_HUB_PRIO       5

/* one task per room, both use these values */
#define TASK_ROOM_STACK     4096
#define TASK_ROOM_PRIO      4

#define TASK_STORAGE_STACK  8192        /* FATFS + SHA-256 + line buffers */
#define TASK_STORAGE_PRIO   3

#define TASK_UI_STACK       4096
#define TASK_UI_PRIO        2

/* large: the console line editor and command handlers use printf heavily */
#define TASK_CLI_STACK      8192
#define TASK_CLI_PRIO       1
