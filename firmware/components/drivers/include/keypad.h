/* Interrupt-driven 4x4 matrix keypad (adapted from the lab ds18b20 project). */
#pragma once

/* Called from the keypad task (never from the ISR), once per press. */
typedef void (*keypad_cb_t)(char key);

void keypad_init(keypad_cb_t on_key);
