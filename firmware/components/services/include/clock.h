/* Time keeping: DS1307 RTC (local time, survives power-off) -> system clock.
 * Timezone is Africa/Kampala (EAT, UTC+3). Day 2 adds SNTP when online. */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include "esp_err.h"

/* Reads the RTC into the system clock. If the RTC was never set, falls back
 * to the firmware build time and writes that into the RTC. */
esp_err_t clock_init(void);

/* Set local time on both the system clock and the RTC. */
esp_err_t clock_set(const struct tm *local);

void clock_local(struct tm *out);

/* Monotonic milliseconds since boot (for timeouts, never jumps). */
uint32_t clock_ms(void);

/* strftime on the current local time. */
void clock_format(char *out, size_t len, const char *fmt);
