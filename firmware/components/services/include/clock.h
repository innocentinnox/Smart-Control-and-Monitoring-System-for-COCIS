/* Time keeping: DS1307 RTC (local time, survives power-off) -> system clock.
 * Timezone is Africa/Kampala (EAT, UTC+3). Day 2 adds SNTP when online.
 *
 * Two kinds of time are offered:
 *   - wall-clock local time (clock_local / clock_format): for schedules and
 *     log timestamps. It can jump when an admin sets the clock.
 *   - monotonic milliseconds (clock_ms): for timeouts and deadlines. It
 *     never jumps, so a clock change cannot extend or cut a timeout.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include "esp_err.h"

/* Reads the RTC into the system clock. If the RTC was never set, falls back
 * to the firmware build time and writes that into the RTC.
 * Also sets the TZ environment variable so localtime() returns EAT.
 * Call once at boot, before anything reads the time.
 *
 * @return ESP_OK, or the error of writing the fallback time to the RTC
 *         (the system clock is still set in that case).
 */
esp_err_t clock_init(void);

/* Set local time on both the system clock and the RTC.
 *
 * @param local  local (EAT) date/time; tm_wday and tm_yday are ignored and
 *               recomputed, out-of-range fields are normalised by mktime().
 * @return ESP_OK, or the DS1307 write error (system clock is set anyway).
 */
esp_err_t clock_set(const struct tm *local);

/* Current local (EAT) date/time broken down into *out. */
void clock_local(struct tm *out);

/* Monotonic milliseconds since boot (for timeouts, never jumps).
 * 32-bit, so it wraps after about 49.7 days; compare deadlines with
 * time_reached_ms() (cocis_types.h), which handles the wrap. */
uint32_t clock_ms(void);

/* strftime on the current local time.
 *
 * @param out  output buffer.
 * @param len  size of out.
 * @param fmt  strftime format, e.g. "%Y-%m-%d %H:%M:%S".
 */
void clock_format(char *out, size_t len, const char *fmt);
