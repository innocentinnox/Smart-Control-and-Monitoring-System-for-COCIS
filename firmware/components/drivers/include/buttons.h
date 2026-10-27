/* Debounced push-buttons (active low, internal pull-up).
 *
 * One ISR for all buttons pushes the button index into a queue; one task
 * confirms the press after the debounce time and calls the callback. Any
 * number of button groups can be registered, each with its own callback.
 */
#pragma once

#include <stddef.h>
#include "driver/gpio.h"

typedef void (*buttons_cb_t)(size_t index);

/* pins[] must stay valid (use a static const array). */
void buttons_add_group(const gpio_num_t *pins, size_t count, buttons_cb_t on_press);
