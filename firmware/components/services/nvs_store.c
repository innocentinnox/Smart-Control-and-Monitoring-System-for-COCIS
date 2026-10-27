#include "nvs_store.h"

#include "nvs.h"
#include "nvs_flash.h"

#define NAMESPACE "cocis"

esp_err_t nvs_store_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());     /* partition layout changed: start clean */
        err = nvs_flash_init();
    }
    return err;
}

esp_err_t nvs_store_load(const char *key, void *data, size_t len)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) {
        return err;
    }
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
    err = nvs_set_blob(h, key, data, len);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}
