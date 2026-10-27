#include "users.h"

#include <ctype.h>
#include <string.h>

#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs_store.h"
#include "storage.h"

static const char *TAG = "users";
#define NVS_KEY "users"

/* Demo cards: one per Wokwi push-button (see docs/event-schema.md 1). */
static const struct {
    card_uid_t uid;
    const char *name;
    role_t role;
    const char *pin;
} DEMO_CARDS[DEMO_CARD_COUNT] = {
    { 0xA1B2C3D4, "Alice", ROLE_STUDENT,  NULL   },
    { 0xB2C3D4E5, "Bob",   ROLE_STAFF,    NULL   },
    { 0xC3D4E5F6, "Carol", ROLE_SECURITY, NULL   },
    { 0xD4E5F6A7, "Admin", ROLE_ADMIN,    "1234" },
};

/* The whole registry is one blob, saved atomically after every change. */
typedef struct {
    uint32_t count;
    user_t   users[USERS_MAX];
} registry_t;

static registry_t        s_reg;
static SemaphoreHandle_t s_lock;

#define LOCK()   xSemaphoreTake(s_lock, portMAX_DELAY)
#define UNLOCK() xSemaphoreGive(s_lock)

/* ---------------- Helpers (call with the lock held) ---------------- */
static user_t *find_locked(card_uid_t uid)
{
    for (uint32_t i = 0; i < s_reg.count; i++) {
        if (s_reg.users[i].uid == uid) {
            return &s_reg.users[i];
        }
    }
    return NULL;
}

static void hash_pin(const uint8_t salt[PIN_SALT_LEN], const char *pin, uint8_t out[SHA256_LEN])
{
    cocis_sha256(salt, PIN_SALT_LEN, pin, strlen(pin), out);
}

static void set_pin(user_t *u, const char *pin)
{
    u->has_pin = pin != NULL;
    if (pin) {
        esp_fill_random(u->salt, sizeof(u->salt));
        hash_pin(u->salt, pin, u->pin_hash);
    }
}

static esp_err_t save_locked(void)
{
    return nvs_store_save(NVS_KEY, &s_reg, sizeof(s_reg));
}

static bool name_valid(const char *s)
{
    size_t n = s ? strlen(s) : 0;
    if (n == 0 || n >= USER_NAME_MAX) {
        return false;
    }
    for (; *s; s++) {
        if (!isalnum((unsigned char)*s) && *s != '_') {
            return false;           /* keeps log fields free of spaces and '|' */
        }
    }
    return true;
}

static bool pin_valid(const char *s)
{
    size_t n = strlen(s);
    if (n < 4 || n > PIN_MAX_LEN) {
        return false;
    }
    for (; *s; s++) {
        if (!isdigit((unsigned char)*s)) {
            return false;
        }
    }
    return true;
}

static void log_user(const char *action, const user_t *u, card_uid_t by)
{
    cocis_log(LOG_USER, "action=%s uid=%08lX name=%s role=%s by=%08lX", action,
              (unsigned long)u->uid, u->name, role_str(u->role), (unsigned long)by);
}

/* ---------------- Public API ---------------- */
esp_err_t users_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    if (nvs_store_load(NVS_KEY, &s_reg, sizeof(s_reg)) == ESP_OK && s_reg.count <= USERS_MAX) {
        ESP_LOGI(TAG, "%lu users loaded", (unsigned long)s_reg.count);
        return ESP_OK;
    }

    memset(&s_reg, 0, sizeof(s_reg));
    for (int i = 0; i < DEMO_CARD_COUNT; i++) {
        user_t *u = &s_reg.users[s_reg.count++];
        u->uid = DEMO_CARDS[i].uid;
        strlcpy(u->name, DEMO_CARDS[i].name, sizeof(u->name));
        u->role    = DEMO_CARDS[i].role;
        u->enabled = true;
        set_pin(u, DEMO_CARDS[i].pin);
    }
    ESP_LOGI(TAG, "First boot: seeded %d demo cards", DEMO_CARD_COUNT);
    return save_locked();
}

bool users_find(card_uid_t uid, user_t *out)
{
    LOCK();
    user_t *u = find_locked(uid);
    if (u && out) {
        *out = *u;
    }
    UNLOCK();
    return u != NULL;
}

size_t users_snapshot(user_t *out, size_t max)
{
    LOCK();
    size_t n = s_reg.count < max ? s_reg.count : max;
    memcpy(out, s_reg.users, n * sizeof(user_t));
    UNLOCK();
    return n;
}

esp_err_t users_add(card_uid_t uid, const char *name, role_t role, const char *pin, card_uid_t by)
{
    if (uid == CARD_UID_NONE || !name_valid(name) || role >= ROLE_COUNT || (pin && !pin_valid(pin))) {
        return ESP_ERR_INVALID_ARG;
    }
    LOCK();
    esp_err_t err = ESP_OK;
    user_t *u = find_locked(uid);
    if (u) {
        err = ESP_ERR_INVALID_STATE;            /* already registered */
    } else if (s_reg.count >= USERS_MAX) {
        err = ESP_ERR_NO_MEM;
    } else {
        u = &s_reg.users[s_reg.count++];
        *u = (user_t){ .uid = uid, .role = role, .enabled = true };
        strlcpy(u->name, name, sizeof(u->name));
        set_pin(u, pin);
        err = save_locked();
        log_user("ADD", u, by);
    }
    UNLOCK();
    return err;
}

esp_err_t users_remove(card_uid_t uid, card_uid_t by)
{
    LOCK();
    user_t *u = find_locked(uid);
    esp_err_t err = ESP_ERR_NOT_FOUND;
    if (u) {
        log_user("DEL", u, by);
        *u = s_reg.users[--s_reg.count];        /* order does not matter: swap-remove */
        err = save_locked();
    }
    UNLOCK();
    return err;
}

esp_err_t users_set_enabled(card_uid_t uid, bool enabled, card_uid_t by)
{
    LOCK();
    user_t *u = find_locked(uid);
    esp_err_t err = ESP_ERR_NOT_FOUND;
    if (u) {
        u->enabled = enabled;
        err = save_locked();
        log_user(enabled ? "ENABLE" : "DISABLE", u, by);
    }
    UNLOCK();
    return err;
}

bool users_verify_pin(card_uid_t uid, const char *pin)
{
    uint8_t calc[SHA256_LEN];
    bool ok = false;
    LOCK();
    user_t *u = find_locked(uid);
    if (u && u->has_pin && pin) {
        hash_pin(u->salt, pin, calc);
        ok = memcmp(calc, u->pin_hash, SHA256_LEN) == 0;
    }
    UNLOCK();
    return ok;
}

card_uid_t users_demo_card(int i)
{
    return (i >= 0 && i < DEMO_CARD_COUNT) ? DEMO_CARDS[i].uid : CARD_UID_NONE;
}
