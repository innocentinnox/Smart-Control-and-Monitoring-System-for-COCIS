/* DS1307 real-time clock: keeps local time while the system is off/offline. */
#pragma once

#include <time.h>
#include "esp_err.h"

esp_err_t ds1307_init(void);

/* Returns ESP_ERR_INVALID_STATE if the clock was halted (never set). */
esp_err_t ds1307_read(struct tm *out);
esp_err_t ds1307_write(const struct tm *t);
