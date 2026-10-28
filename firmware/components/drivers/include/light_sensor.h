/* LDR light sensors on ADC1, shared by both room tasks.
 * One ADC unit, two readers: a mutex serialises the conversions.
 *
 * Each room has a Wokwi photoresistor (LDR) module whose analog output (AO)
 * is read by the ESP32-S3's ADC (analog-to-digital converter) and converted
 * to an approximate light level in lux. Only ADC1 pins are accepted because
 * ADC2 is shared with Wi-Fi on the ESP32 family and is unreliable while
 * Wi-Fi is running.
 */
#pragma once

#include <stdint.h>
#include "esp_err.h"

/* Create the ADC1 unit and the mutex. Call once at boot, before
 * light_sensor_add().
 * @return ESP_OK, or an error from adc_oneshot_new_unit()
 */
esp_err_t light_sensor_init(void);

/* Add an LDR on an ADC1-capable GPIO; returns a handle (0, 1, ...).
 * @param gpio  GPIO number wired to the module's AO pin (must be an ADC1
 *              pin, GPIO1..GPIO10 on the ESP32-S3)
 * @return handle to pass to light_sensor_read_lux(), or -1 if the pin is not
 *         on ADC1, the channel could not be configured, or 4 sensors are
 *         already registered
 * Call at boot only (not protected by the mutex).
 */
int light_sensor_add(int gpio);

/* Approximate illuminance in lux, or -1 on error.
 * @param handle  value returned by light_sensor_add()
 * @return 0..65535 lux (capped), or -1 for a bad handle / failed ADC read
 * Safe from any task; may wait briefly while another task's conversion
 * finishes.
 */
int light_sensor_read_lux(int handle);
