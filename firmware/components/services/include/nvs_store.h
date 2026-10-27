/* Load/save one fixed-size struct as an NVS blob (namespace "cocis").
 * nvs_commit() makes each save atomic: after a power cut you get either
 * the old blob or the new one, never half of each. */
#pragma once

#include <stddef.h>
#include "esp_err.h"

esp_err_t nvs_store_init(void);

/* ESP_ERR_NVS_NOT_FOUND (or a size mismatch -> ESP_ERR_INVALID_SIZE) means
 * "use defaults". */
esp_err_t nvs_store_load(const char *key, void *data, size_t len);
esp_err_t nvs_store_save(const char *key, const void *data, size_t len);
