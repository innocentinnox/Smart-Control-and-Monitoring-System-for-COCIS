/* System configuration (access schedule, lockdown), persisted in NVS.
 *
 * Every change bumps `version`. Day 2 uses it to accept a cloud command only
 * if it is newer than what the device already has. Readers get a consistent
 * copy under a mutex, so they never see half an update.
 */
#pragma once

#include "access_policy.h"
#include "esp_err.h"

typedef struct {
    access_schedule_t schedule;
    bool              lockdown;
    uint32_t          version;
} sys_config_t;

esp_err_t config_init(void);
void      config_get(sys_config_t *out);

/* by = admin card making the change (audit log). */
esp_err_t config_set_schedule(const access_schedule_t *s, card_uid_t by);
esp_err_t config_set_lockdown(bool on, card_uid_t by);

/* "07:00-22:00 Mon-Fri"-style text for menus and logs. */
void config_schedule_str(const access_schedule_t *s, char *out, size_t len);
