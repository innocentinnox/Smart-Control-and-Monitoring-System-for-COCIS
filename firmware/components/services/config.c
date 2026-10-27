#include "config.h"

#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs_store.h"
#include "sdkconfig.h"
#include "storage.h"

#define NVS_KEY "config"

static sys_config_t      s_cfg;
static SemaphoreHandle_t s_lock;

esp_err_t config_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    if (nvs_store_load(NVS_KEY, &s_cfg, sizeof(s_cfg)) == ESP_OK && schedule_is_valid(&s_cfg.schedule)) {
        return ESP_OK;
    }
    s_cfg = (sys_config_t){
        .schedule = {
            .open_min  = CONFIG_COCIS_OPEN_HOUR * 60,
            .close_min = CONFIG_COCIS_CLOSE_HOUR * 60,
            .days_mask = DAYS_MON_SAT,
        },
        .version = 1,
    };
    return nvs_store_save(NVS_KEY, &s_cfg, sizeof(s_cfg));
}

void config_get(sys_config_t *out)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_cfg;
    xSemaphoreGive(s_lock);
}

/* Apply a change to a copy, persist it, and only then make it live. */
static esp_err_t commit(const sys_config_t *next)
{
    sys_config_t candidate = *next;
    candidate.version = s_cfg.version + 1;
    esp_err_t err = nvs_store_save(NVS_KEY, &candidate, sizeof(candidate));
    if (err == ESP_OK) {
        s_cfg = candidate;
    }
    return err;
}

esp_err_t config_set_schedule(const access_schedule_t *s, card_uid_t by)
{
    if (!schedule_is_valid(s)) {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    sys_config_t next = s_cfg;
    next.schedule = *s;
    esp_err_t err = commit(&next);
    uint32_t version = s_cfg.version;
    xSemaphoreGive(s_lock);

    if (err == ESP_OK) {
        char text[40];
        config_schedule_str(s, text, sizeof(text));
        cocis_log(LOG_CONFIG, "key=schedule value=%s version=%lu by=%08lX", text,
                  (unsigned long)version, (unsigned long)by);
    }
    return err;
}

esp_err_t config_set_lockdown(bool on, card_uid_t by)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    sys_config_t next = s_cfg;
    next.lockdown = on;
    esp_err_t err = commit(&next);
    xSemaphoreGive(s_lock);

    if (err == ESP_OK) {
        cocis_log(LOG_CONFIG, "key=lockdown value=%d by=%08lX", on, (unsigned long)by);
    }
    return err;
}

void config_schedule_str(const access_schedule_t *s, char *out, size_t len)
{
    static const char days[] = "SMTWTFS";       /* Sun..Sat initials */
    char mask[8];
    for (int d = 0; d < 7; d++) {
        mask[d] = (s->days_mask & (1u << d)) ? days[d] : '-';
    }
    mask[7] = '\0';
    snprintf(out, len, "%02d:%02d-%02d:%02d_%s", s->open_min / 60, s->open_min % 60,
             s->close_min / 60, s->close_min % 60, mask);
}
