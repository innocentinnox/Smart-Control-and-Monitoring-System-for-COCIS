/* Occupancy ledger (see occupancy.h).
 *
 * Data: s_ledger in RAM, saved as the NVS blob "occupancy" after every
 * change. The table is an unordered array; entries 0..count-1 are valid.
 */
#include "occupancy.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs_store.h"

static const char *TAG = "occupancy";
#define NVS_KEY "occupancy"         /* NVS key of the ledger blob */

/* Exact layout of the NVS blob. */
typedef struct {
    uint32_t   count;                   /* used entries, 0..OCCUPANCY_MAX */
    presence_t people[OCCUPANCY_MAX];
} ledger_t;

static ledger_t          s_ledger;  /* live ledger */
static SemaphoreHandle_t s_lock;    /* FreeRTOS mutex guarding s_ledger */

/* Index of `uid` in the ledger, or -1 if not inside. Call with s_lock held. */
static int index_of_locked(card_uid_t uid)
{
    for (uint32_t i = 0; i < s_ledger.count; i++) {
        if (s_ledger.people[i].uid == uid) {
            return (int)i;
        }
    }
    return -1;
}

esp_err_t occupancy_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    /* Start empty if nothing is stored, the size changed, or the stored
     * count is impossible (corrupt data would otherwise index past the end). */
    if (nvs_store_load(NVS_KEY, &s_ledger, sizeof(s_ledger)) != ESP_OK ||
        s_ledger.count > OCCUPANCY_MAX) {
        memset(&s_ledger, 0, sizeof(s_ledger));
    }
    ESP_LOGI(TAG, "%lu people inside at boot", (unsigned long)s_ledger.count);
    return ESP_OK;
}

bool occupancy_is_inside(card_uid_t uid)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool inside = index_of_locked(uid) >= 0;
    xSemaphoreGive(s_lock);
    return inside;
}

void occupancy_set(card_uid_t uid, bool inside)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int i = index_of_locked(uid);
    bool changed = false;
    /* Entering: append if not already inside and there is room. The entry
     * time is wall-clock time (time(NULL)) so "who" can show it. */
    if (inside && i < 0 && s_ledger.count < OCCUPANCY_MAX) {
        s_ledger.people[s_ledger.count++] = (presence_t){ .uid = uid, .since = time(NULL) };
        changed = true;
    /* Leaving: swap-remove (last entry moves into the freed slot). */
    } else if (!inside && i >= 0) {
        s_ledger.people[i] = s_ledger.people[--s_ledger.count];
        changed = true;
    }
    /* Only write flash when the ledger really changed (flash has limited
     * write cycles). The save result is not checked. */
    if (changed) {
        nvs_store_save(NVS_KEY, &s_ledger, sizeof(s_ledger));
    }
    xSemaphoreGive(s_lock);
}

size_t occupancy_count(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    size_t n = s_ledger.count;
    xSemaphoreGive(s_lock);
    return n;
}

size_t occupancy_snapshot(presence_t *out, size_t max)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    size_t n = s_ledger.count < max ? s_ledger.count : max;
    memcpy(out, s_ledger.people, n * sizeof(presence_t));
    xSemaphoreGive(s_lock);
    return n;
}
