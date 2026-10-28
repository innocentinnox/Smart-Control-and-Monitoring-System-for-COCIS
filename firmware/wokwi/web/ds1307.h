/* DS1307 real-time clock: keeps local time while the system is off/offline.
 *
 * The DS1307 is a battery-backed clock chip on the shared I2C bus at address
 * 0x68 (I2C_ADDR_RTC). It stores seconds..year in BCD registers. The clock
 * service reads it at boot (so the time is right even without Wi-Fi/NTP) and
 * writes it back whenever it learns the correct time.
 */
#pragma once

#include <time.h>
#include "esp_err.h"

/* Attach the RTC to the shared I2C bus. i2c_bus_init() must run first.
 * Does not talk to the chip yet, so it succeeds even if the RTC is missing.
 * @return ESP_OK, or an error from the I2C driver
 * Call once at boot.
 */
esp_err_t ds1307_init(void);

/* Read the current local time from the RTC.
 * @param out  filled with sec, min, hour (24 h), wday, mday, mon, year in
 *             the usual struct tm units (tm_mon 0..11, tm_year = year-1900,
 *             tm_wday 0..6); only written when ESP_OK is returned
 * @return ESP_OK on success, an I2C error (e.g. ESP_ERR_TIMEOUT) if the chip
 *         does not answer, or ESP_ERR_INVALID_STATE (below)
 * Blocks for one short I2C transaction (100 ms timeout).
 */
/* Returns ESP_ERR_INVALID_STATE if the clock was halted (never set). */
esp_err_t ds1307_read(struct tm *out);

/* Set the RTC time (this also starts the oscillator if it was halted).
 * @param t  local time to store; only sec, min, hour, wday, mday, mon and
 *           year (20xx only: year % 100 is stored) are used
 * @return ESP_OK, or an I2C error if the chip does not answer
 * Blocks for one short I2C transaction (100 ms timeout).
 */
esp_err_t ds1307_write(const struct tm *t);
