/* System configuration (access schedule, lockdown), persisted in NVS.
 *
 * Data: one sys_config_t in RAM, saved as the NVS blob "config". On first
 * boot the schedule comes from menuconfig (CONFIG_COCIS_OPEN_HOUR /
 * CONFIG_COCIS_CLOSE_HOUR, Mon-Sat); after that it only changes through
 * the admin "schedule set" / "lockdown" commands.
 *
 * Every change bumps `version`. Day 2 uses it to accept a cloud command only
 * if it is newer than what the device already has. Readers get a consistent
 * copy under a mutex, so they never see half an update.
 *
 * Callers: hub (access decisions), CLI (status, schedule, lockdown).
 */
#pragma once

#include "access_policy.h"
#include "esp_err.h"

/* The whole persisted configuration (stored as one NVS blob). */
typedef struct {
    access_schedule_t schedule;     /* opening hours + working days */
    bool              lockdown;     /* true = only security/admin may pass */
    uint32_t          version;      /* starts at 1, +1 on every saved change */
} sys_config_t;

/* Load the config from NVS, or create and save the default one if it is
 * missing or holds an invalid schedule. Call once at boot.
 *
 * @return ESP_OK, or the NVS error if saving the defaults failed.
 */
esp_err_t config_init(void);

/* Copy the current config into *out. Safe from any task (mutex). */
void      config_get(sys_config_t *out);

/* Replace the opening hours. The new config is saved to NVS first and only
 * becomes live if the save succeeded.
 * by = admin card making the change (audit log).
 *
 * @param s   new schedule (open/close in minutes after midnight, days mask).
 * @param by  admin UID written to the CONFIG log line.
 * @return ESP_OK, ESP_ERR_INVALID_ARG (schedule_is_valid() failed, e.g.
 *         open == close), or an NVS save error.
 */
esp_err_t config_set_schedule(const access_schedule_t *s, card_uid_t by);

/* Switch lockdown on or off (saved and logged like the schedule).
 *
 * @param on  true = lockdown: only security/admin cards are let through.
 * @param by  admin UID for the audit log.
 * @return ESP_OK or an NVS save error.
 */
esp_err_t config_set_lockdown(bool on, card_uid_t by);

/* "07:00-22:00 Mon-Fri"-style text for menus and logs.
 * Actual format: "HH:MM-HH:MM_SMTWTFS", one letter per working day and '-'
 * for a closed day, Sunday first, e.g. "07:00-22:00_-MTWTFS". It has no
 * spaces, so it fits in a "key=value" log field.
 *
 * @param s    schedule to describe.
 * @param out  output buffer (20 bytes is enough; callers use 40).
 * @param len  size of out.
 */
void config_schedule_str(const access_schedule_t *s, char *out, size_t len);
