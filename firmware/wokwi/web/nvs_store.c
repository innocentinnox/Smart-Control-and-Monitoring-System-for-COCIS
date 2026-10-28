/* NVS blob helper (see nvs_store.h).
 *
 * Every call opens the "cocis" namespace, does one read or write, and closes
 * the handle again. Opening is cheap, and not keeping a handle open means
 * there is no shared state here that would need a mutex.
 */
#include "nvs_store.h"

#include "nvs.h"
#include "nvs_flash.h"

/* All keys of this project live in one NVS namespace (a "folder" of keys). */
#define NAMESPACE "cocis"

esp_err_t nvs_store_init(void)
{
    esp_err_t err = nvs_flash_init();
    /* These two errors mean the partition cannot be used as it is: it has no
     * free pages left, or it was formatted by a different NVS version. The
     * standard ESP-IDF recovery is to erase it and start again. */
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());     /* partition layout changed: start clean */
        err = nvs_flash_init();
    }
    return err;
}

esp_err_t nvs_store_load(const char *key, void *data, size_t len)
{
    nvs_handle_t h;
    /* Read-only open fails with ESP_ERR_NVS_NOT_FOUND on a fresh device,
     * before anything was ever saved: the caller then uses its defaults. */
    esp_err_t err = nvs_open(NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) {
        return err;
    }
    /* `stored` goes in as the buffer size and comes back as the size of the
     * blob in flash. */
    size_t stored = len;
    err = nvs_get_blob(h, key, data, &stored);
    nvs_close(h);
    if (err == ESP_OK && stored != len) {
        return ESP_ERR_INVALID_SIZE;            /* struct changed since it was saved */
    }
    return err;
}

esp_err_t nvs_store_save(const char *key, const void *data, size_t len)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    /* nvs_set_blob() only stages the new value; nvs_commit() is what writes
     * it to flash. Skip the commit if staging already failed. */
    err = nvs_set_blob(h, key, data, len);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}
