/* The one I2C master bus shared by the LCD and the RTC.
 * The IDF i2c_master driver locks the bus per transaction, so devices on
 * it can be used from different tasks without corrupting each other.
 *
 * I2C is a two-wire bus (SDA = data, SCL = clock). The ESP32 is the
 * "master" that starts every transfer; each device ("slave") answers to its
 * own 7-bit address: LCD backpack 0x27, DS1307 RTC 0x68. Pins and the bus
 * speed (100 kHz) come from board_pins.h.
 */
#pragma once

#include "driver/i2c_master.h"

/* Create the I2C master bus on PIN_I2C_SDA / PIN_I2C_SCL (port 0).
 * Must run before any i2c_bus_add_device() call.
 * @return ESP_OK, or an error from i2c_new_master_bus()
 * Call once at boot.
 */
esp_err_t i2c_bus_init(void);

/* Attach a 7-bit device to the shared bus.
 * @param addr  7-bit I2C address of the device (e.g. 0x27, 0x68)
 * @param out   receives the device handle used for later
 *              i2c_master_transmit()/receive() calls
 * @return ESP_OK, or an error from the IDF driver. This only registers the
 *         device; it does not check that something answers at addr.
 * Call at boot, after i2c_bus_init().
 */
esp_err_t i2c_bus_add_device(uint8_t addr, i2c_master_dev_handle_t *out);
