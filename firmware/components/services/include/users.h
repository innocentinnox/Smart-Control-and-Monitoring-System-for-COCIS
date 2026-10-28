/* User / card registry, persisted in NVS.
 *
 * Holds every registered RFID card: owner name, role, enabled flag and
 * (for admins) a PIN. The whole table lives in RAM as one struct and is
 * saved to NVS (key "users") as one blob after every change.
 *
 * PINs are never stored: only SHA-256(random salt || PIN). A random salt per
 * user means two users with the same PIN get different hashes, and a dump of
 * flash does not reveal the PIN directly.
 *
 * All functions are thread-safe (one mutex); lookups return a copy so callers
 * never hold a pointer into the shared table. Changes are logged as USER
 * events.
 *
 * Callers: hub (access decisions), auth (admin check + PIN check), CLI
 * ("users" list, "user add/del/enable/disable").
 */
#pragma once

#include "cocis_crypto.h"
#include "cocis_types.h"
#include "esp_err.h"

#define USERS_MAX      32           /* capacity of the registry (cards) */
#define PIN_SALT_LEN   8            /* random salt bytes per user */
/* Exactly 4 digits so the same PIN can be typed on the 4x4 keypad or the
 * console, and pin_valid() can reject anything else. */
#define PIN_LEN        4            /* PINs are exactly 4 digits */
#define DEMO_CARD_COUNT 4           /* cards seeded on first boot */

/* One registered card. Stored as-is inside the NVS blob, so changing this
 * struct changes the blob size (see REGISTRY_VERSION in users.c). */
typedef struct {
    card_uid_t uid;                 /* card serial number, never CARD_UID_NONE */
    char       name[USER_NAME_MAX]; /* NUL-terminated, letters/digits/_ only */
    role_t     role;                /* student / staff / security / admin */
    bool       enabled;             /* false = card is blocked at the doors */
    bool       has_pin;             /* salt and pin_hash are only valid if true */
    uint8_t    salt[PIN_SALT_LEN];  /* random bytes mixed into the hash */
    uint8_t    pin_hash[SHA256_LEN];/* SHA-256(salt || PIN) */
} user_t;

/* Loads from NVS, or seeds the demo cards on first boot.
 * Also re-seeds when the stored blob has a different REGISTRY_VERSION or size.
 * Call once at boot, after nvs_store_init().
 *
 * @return ESP_OK, or the NVS error if saving the freshly seeded table failed.
 */
esp_err_t users_init(void);

/* Look up a card. Safe from any task (mutex).
 *
 * @param uid  card to find.
 * @param out  receives a copy of the user record; may be NULL if only
 *             "is it registered?" is wanted.
 * @return true if the card is registered (enabled or not).
 */
bool   users_find(card_uid_t uid, user_t *out);

/* Copy up to `max` user records into `out` (for listings). Safe from any task.
 *
 * @param out  caller's array of at least `max` entries.
 * @param max  array capacity; USERS_MAX copies everything.
 * @return number of records copied.
 */
size_t users_snapshot(user_t *out, size_t max);

/* Register a new card. The user starts enabled.
 * by = admin card making the change (for the audit log).
 * name: 1..15 chars, letters/digits/_ only. pin: NULL or exactly PIN_LEN digits.
 *
 * @param uid   new card UID (must not be CARD_UID_NONE).
 * @param name  owner name, see rule above.
 * @param role  one of role_t (below ROLE_COUNT).
 * @param pin   NULL = no PIN, else the 4-digit PIN as text (only admins need one).
 * @param by    admin UID written to the USER log line.
 * @return ESP_OK, ESP_ERR_INVALID_ARG (bad uid/name/role/pin),
 *         ESP_ERR_INVALID_STATE (card already registered),
 *         ESP_ERR_NO_MEM (registry full) or an NVS save error.
 */
esp_err_t users_add(card_uid_t uid, const char *name, role_t role, const char *pin, card_uid_t by);

/* Delete a card from the registry.
 *
 * @param uid  card to remove.
 * @param by   admin UID for the audit log.
 * @return ESP_OK, ESP_ERR_NOT_FOUND, or an NVS save error.
 */
esp_err_t users_remove(card_uid_t uid, card_uid_t by);

/* Block or unblock a card without deleting it.
 *
 * @param uid      card to change.
 * @param enabled  true = may pass the doors again, false = blocked.
 * @param by       admin UID for the audit log.
 * @return ESP_OK, ESP_ERR_NOT_FOUND, or an NVS save error.
 */
esp_err_t users_set_enabled(card_uid_t uid, bool enabled, card_uid_t by);

/* Check a typed PIN against the stored salted hash. Safe from any task.
 *
 * @param uid  card whose PIN is checked.
 * @param pin  typed PIN as text (may be NULL -> false).
 * @return true only if the card exists, has a PIN, and the hash matches.
 */
bool users_verify_pin(card_uid_t uid, const char *pin);

/* UID of demo card i (0..3) = the Wokwi card buttons 1..4.
 *
 * @param i  button index 0..DEMO_CARD_COUNT-1.
 * @return that card's UID, or CARD_UID_NONE if i is out of range.
 */
card_uid_t users_demo_card(int i);
