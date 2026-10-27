/* The one I2C master bus shared by the LCD and the RTC.
 * The IDF i2c_master driver locks the bus per transaction, so devices on
 * it can be used from different tasks without corrupting each other. */
#pragma once

#include "driver/i2c_master.h"

esp_err_t i2c_bus_init(void);

/* Attach a 7-bit device to the shared bus. */
esp_err_t i2c_bus_add_device(uint8_t addr, i2c_master_dev_handle_t *out);
