/* Load/save one fixed-size struct as an NVS blob (namespace "cocis").
 *
 * NVS (Non-Volatile Storage) is ESP-IDF's small key/value database that
 * lives in a dedicated flash partition. Values survive reboots and power
 * cuts. Each service (users, config, occupancy) keeps its whole state in one
 * C struct and stores it under one key as a "blob" (raw bytes), so loading
 * is a single read and saving a single write.
 *
 * nvs_commit() makes each save atomic: after a power cut you get either
 * the old blob or the new one, never half of each.
 *
 * Callers: users.c, config.c, occupancy.c. They do their own locking; this
 * module keeps no state of its own.
 */
#pragma once

#include <stddef.h>
#include "esp_err.h"

/* Initialise the NVS flash partition. Call once at boot, before any other
 * service touches NVS (main.c does this first).
 * If the partition is full or was written by a newer NVS format it is
 * erased and re-initialised, so all stored settings are lost in that case.
 *
 * @return ESP_OK, or the error from nvs_flash_init().
 */
esp_err_t nvs_store_init(void);

/* Read the blob stored under `key` into `data`.
 *
 * @param key   NVS key name (max 15 chars), e.g. "users".
 * @param data  destination buffer (usually a pointer to the service's struct).
 * @param len   size of `data` in bytes; the stored blob must be exactly this
 *              size, otherwise it was saved by an older firmware whose struct
 *              layout differed.
 * @return ESP_OK on success.
 *         ESP_ERR_NVS_NOT_FOUND (or a size mismatch -> ESP_ERR_INVALID_SIZE)
 *         means "use defaults". `data` may be partly overwritten on error,
 *         so callers reset it themselves.
 */
esp_err_t nvs_store_load(const char *key, void *data, size_t len);

/* Write `len` bytes from `data` under `key` and commit them to flash.
 *
 * @param key   NVS key name (max 15 chars).
 * @param data  bytes to store (usually the service's whole struct).
 * @param len   number of bytes.
 * @return ESP_OK once the data is safely in flash, otherwise an NVS error
 *         (e.g. ESP_ERR_NVS_NOT_ENOUGH_SPACE).
 */
esp_err_t nvs_store_save(const char *key, const void *data, size_t len);
