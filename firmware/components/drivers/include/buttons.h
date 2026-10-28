/* Debounced push-buttons (active low, internal pull-up).
 *
 * Wiring: one leg of each button to its GPIO, the other leg to GND. The
 * internal pull-up keeps the pin HIGH while released; pressing pulls it LOW
 * ("active low"), so a press is a falling edge.
 *
 * One ISR for all buttons pushes the button index into a queue; one task
 * confirms the press after the debounce time and calls the callback. Any
 * number of button groups can be registered, each with its own callback.
 *
 * Why debounce: a mechanical contact "bounces" for a few ms when pressed,
 * producing many edges. Without debouncing one press would look like many.
 */
#pragma once

#include <stddef.h>
#include "driver/gpio.h"

/* Press callback.
 * @param index  position of the pressed button in the pins[] array that was
 *               passed to buttons_add_group() (0 = first pin)
 * Runs in the buttons task (normal task context, never in the ISR), so it
 * may log, send to queues, etc. Keep it short: while it runs no other
 * button press is handled.
 */
typedef void (*buttons_cb_t)(size_t index);

/* Register a group of buttons that share one callback.
 * The first call also creates the shared queue and the buttons task.
 * @param pins      array of GPIOs, one per button
 * @param count     number of entries in pins[] (at most 256 per group)
 * @param on_press  called once per confirmed press with the button's index
 * Call from app_main at boot only (not thread-safe). At most 4 groups.
 */
/* pins[] must stay valid (use a static const array). */
void buttons_add_group(const gpio_num_t *pins, size_t count, buttons_cb_t on_press);
