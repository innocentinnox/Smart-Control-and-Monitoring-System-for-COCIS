/* User / card registry (see users.h).
 *
 * Data: one registry_t in RAM (s_reg), mirrored to the NVS blob "users".
 * Every public function takes the mutex s_lock, works on s_reg, and (for
 * changes) saves the whole blob again before releasing the lock.
 *
 * A FreeRTOS mutex is a lock that only one task can hold at a time; another
 * task calling xSemaphoreTake() on it waits until the holder gives it back.
 * Here it stops e.g. the CLI adding a user while the hub is looking one up.
 */
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
#define NVS_KEY "users"             /* NVS key of the registry blob */

/* Demo cards: the UIDs of the Wokwi MFRC522 preset cards (Blue, Green,
 * Yellow, Red; see docs/event-schema.md 1). They are written into the
 * registry on first boot so the system can be demonstrated straight away.
 * The Key Fob (C0FFEE99) is deliberately left unregistered. Only "Admin" has
 * a PIN (1234), because only admins can log in to the management console. */
static const struct {
    card_uid_t uid;
    const char *name;
    role_t role;
    const char *pin;                /* NULL = no PIN */
} DEMO_CARDS[DEMO_CARD_COUNT] = {
    { 0x01020304, "Alice", ROLE_STUDENT,  NULL   },     /* Blue card   */
    { 0x11223344, "Bob",   ROLE_STAFF,    NULL   },     /* Green card  */
    { 0x55667788, "Carol", ROLE_SECURITY, NULL   },     /* Yellow card */
    { 0xAABBCCDD, "Admin", ROLE_ADMIN,    "1234" },     /* Red card    */
};

/* The whole registry is one blob, saved atomically after every change.
 * Bump REGISTRY_VERSION whenever the stored format or the demo seed changes:
 * an older blob is then discarded and the demo cards are re-seeded.
 * (A size change is caught by nvs_store_load() anyway; the version also
 * catches changes that keep the same size, e.g. a new meaning of a field.) */
#define REGISTRY_VERSION 3

/* Exact layout of the NVS blob. */
typedef struct {
    uint32_t version;               /* REGISTRY_VERSION at the time of saving */
    uint32_t count;                 /* used entries in users[], 0..USERS_MAX */
    user_t   users[USERS_MAX];      /* entries 0..count-1 are valid, unordered */
} registry_t;

static registry_t        s_reg;     /* the live registry (RAM copy of the blob) */
static SemaphoreHandle_t s_lock;    /* guards s_reg */

#define LOCK()   xSemaphoreTake(s_lock, portMAX_DELAY)
#define UNLOCK() xSemaphoreGive(s_lock)

/* ---------------- Helpers (call with the lock held) ---------------- */

/* Linear search for a card (the table is small, 32 entries max).
 * Returns a pointer INTO s_reg, so it is only valid while the lock is held;
 * public functions copy the record out before unlocking. */
static user_t *find_locked(card_uid_t uid)
{
    for (uint32_t i = 0; i < s_reg.count; i++) {
        if (s_reg.users[i].uid == uid) {
            return &s_reg.users[i];
        }
    }
    return NULL;
}

/* out = SHA-256(salt || pin). The salt is hashed first, then the PIN text
 * (without its NUL). Used both when setting and when checking a PIN, so the
 * two always compute the same thing. */
static void hash_pin(const uint8_t salt[PIN_SALT_LEN], const char *pin, uint8_t out[SHA256_LEN])
{
    cocis_sha256(salt, PIN_SALT_LEN, pin, strlen(pin), out);
}

/* Give user `u` a new PIN (or none if pin == NULL).
 * A fresh random salt is drawn from the hardware RNG every time a PIN is
 * set; only the salt and the hash are kept, never the PIN itself. */
static void set_pin(user_t *u, const char *pin)
{
    u->has_pin = pin != NULL;
    if (pin) {
        esp_fill_random(u->salt, sizeof(u->salt));
        hash_pin(u->salt, pin, u->pin_hash);
    }
}

/* Write the whole registry to NVS (one atomic blob write). */
static esp_err_t save_locked(void)
{
    return nvs_store_save(NVS_KEY, &s_reg, sizeof(s_reg));
}

/* Name rule: 1..USER_NAME_MAX-1 (15) chars, letters, digits or '_'.
 * Names end up inside "key=value" log lines, so spaces or separators in a
 * name would break the log format. */
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

/* PIN rule: exactly PIN_LEN (4) decimal digits. `s` must not be NULL. */
static bool pin_valid(const char *s)
{
    if (strlen(s) != PIN_LEN) {
        return false;
    }
    for (; *s; s++) {
        if (!isdigit((unsigned char)*s)) {
            return false;
        }
    }
    return true;
}

/* Write one USER audit record, e.g. "action=ADD uid=... name=... by=...".
 * @param action  "ADD", "DEL", "ENABLE" or "DISABLE".
 * @param u       the affected user (read before it is removed).
 * @param by      admin card that made the change. */
static void log_user(const char *action, const user_t *u, card_uid_t by)
{
    cocis_log(LOG_USER, "action=%s uid=%08lX name=%s role=%s by=%08lX", action,
              (unsigned long)u->uid, u->name, role_str(u->role), (unsigned long)by);
}

/* ---------------- Public API ---------------- */
esp_err_t users_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    /* Keep the stored registry only if it loaded with the right size, has the
     * current format version, and a sane count. Anything else (first boot,
     * older firmware, corrupted data) falls through to re-seeding. */
    if (nvs_store_load(NVS_KEY, &s_reg, sizeof(s_reg)) == ESP_OK &&
        s_reg.version == REGISTRY_VERSION && s_reg.count <= USERS_MAX) {
        ESP_LOGI(TAG, "%lu users loaded", (unsigned long)s_reg.count);
        return ESP_OK;
    }

    /* Start from an empty registry and add the demo cards. No lock needed
     * yet: this runs once at boot before any other task uses the registry. */
    memset(&s_reg, 0, sizeof(s_reg));
    s_reg.version = REGISTRY_VERSION;
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
    /* Copy while locked: the caller gets a stable record, not a pointer that
     * another task could change or swap-remove underneath it. */
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
    /* Validate all input before taking the lock (pure checks, no shared data). */
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
        /* Append at the end, then fill in the record and persist. */
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
        /* Log first: the record is overwritten by the swap-remove below. */
        log_user("DEL", u, by);
        /* Move the last entry into the freed slot and shrink the count:
         * O(1), no gap in the array. */
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
        /* Hash the typed PIN with the SAME salt that was used when the PIN
         * was set; equal hashes mean the PIN is correct. Note: memcmp() stops
         * at the first differing byte, so this compare is not constant-time. */
        hash_pin(u->salt, pin, calc);
        ok = memcmp(calc, u->pin_hash, SHA256_LEN) == 0;
    }
    UNLOCK();
    return ok;
}
