/* User / card registry, persisted in NVS.
 *
 * PINs are never stored: only SHA-256(random salt || PIN). All functions are
 * thread-safe (one mutex); lookups return a copy so callers never hold a
 * pointer into the shared table. Changes are logged as USER events.
 */
#pragma once

#include "cocis_crypto.h"
#include "cocis_types.h"
#include "esp_err.h"

#define USERS_MAX      32
#define PIN_SALT_LEN   8
#define PIN_MAX_LEN    8
#define DEMO_CARD_COUNT 4

typedef struct {
    card_uid_t uid;
    char       name[USER_NAME_MAX];
    role_t     role;
    bool       enabled;
    bool       has_pin;
    uint8_t    salt[PIN_SALT_LEN];
    uint8_t    pin_hash[SHA256_LEN];
} user_t;

/* Loads from NVS, or seeds the demo cards on first boot. */
esp_err_t users_init(void);

bool   users_find(card_uid_t uid, user_t *out);
size_t users_snapshot(user_t *out, size_t max);

/* by = admin card making the change (for the audit log).
 * name: 1..15 chars, letters/digits/_ only. pin: NULL or 4..8 digits. */
esp_err_t users_add(card_uid_t uid, const char *name, role_t role, const char *pin, card_uid_t by);
esp_err_t users_remove(card_uid_t uid, card_uid_t by);
esp_err_t users_set_enabled(card_uid_t uid, bool enabled, card_uid_t by);

bool users_verify_pin(card_uid_t uid, const char *pin);

/* UID of demo card i (0..3) = the Wokwi card buttons 1..4. */
card_uid_t users_demo_card(int i);
