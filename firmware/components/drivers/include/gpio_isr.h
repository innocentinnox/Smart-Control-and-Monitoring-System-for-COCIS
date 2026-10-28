/* The GPIO ISR service is shared by several drivers; install it once.
 *
 * ESP-IDF's "GPIO ISR service" is one interrupt handler that dispatches to
 * a separate per-pin handler (registered with gpio_isr_handler_add()). It
 * must be installed exactly once before any per-pin handler is added; both
 * the keypad and the buttons drivers need it, so they call this helper
 * instead of installing it themselves.
 */
#pragma once

/* Install the GPIO ISR service if it is not installed yet; later calls do
 * nothing. Aborts (ESP_ERROR_CHECK) if IDF fails to install it.
 * Not thread-safe: call only from app_main during boot.
 */
void gpio_isr_service_ensure(void);
