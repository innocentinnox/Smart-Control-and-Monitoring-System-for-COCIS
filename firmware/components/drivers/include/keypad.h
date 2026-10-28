/* Interrupt-driven 4x4 matrix keypad (adapted from the lab ds18b20 project).
 *
 * 16 keys wired as a 4x4 grid: 4 row wires and 4 column wires, each key
 * joins one row to one column. Layout:
 *     1 2 3 A
 *     4 5 6 B
 *     7 8 9 C
 *     * 0 # D
 * Pins come from PIN_KEYPAD_R1..R4 and PIN_KEYPAD_C1..C4 in board_pins.h.
 * No CPU time is used while no key is pressed (see keypad.c).
 */
#pragma once

/* Key callback.
 * @param key  the ASCII character printed on the key: '0'..'9', 'A'..'D',
 *             '*' or '#'
 */
/* Called from the keypad task (never from the ISR), once per press. */
typedef void (*keypad_cb_t)(char key);

/* Configure the keypad pins, start the keypad task and enable the
 * interrupts.
 * @param on_key  called once per key press (may be NULL to ignore keys)
 * Call once at boot from app_main.
 */
void keypad_init(keypad_cb_t on_key);
