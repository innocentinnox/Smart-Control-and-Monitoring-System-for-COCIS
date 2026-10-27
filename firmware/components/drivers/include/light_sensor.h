/* LDR light sensors on ADC1, shared by both room tasks.
 * One ADC unit, two readers: a mutex serialises the conversions. */
#pragma once

#include <stdint.h>
#include "esp_err.h"

esp_err_t light_sensor_init(void);

/* Add an LDR on an ADC1-capable GPIO; returns a handle (0, 1, ...). */
int light_sensor_add(int gpio);

/* Approximate illuminance in lux, or -1 on error. */
int light_sensor_read_lux(int handle);
