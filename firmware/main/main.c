/* CoCIS Smart Control & Monitoring System - boot sequence.
 *
 * app_main only wires the system together; every behaviour lives in a
 * component (see docs/REPORT.md for the architecture):
 *
 *   common      shared types, pin map, task table, SHA-256
 *   core_logic  pure rules: access policy, room lighting, admin 2FA (host-tested)
 *   drivers     I2C/LCD/RTC/keypad/buttons/PWM/ADC
 *   bus         message passing between the three terminals
 *   storage     SD-card gatekeeper task + hash-chained event log
 *   services    users, config, occupancy ledger, auth, clock (mutex + NVS)
 *   ui          LCD + buzzer + LED task
 *   nodes       entrance hub, room nodes, door, RFID front-end
 *   cli         serial management menu
 *
 * Order matters: time first (log timestamps), then the log, then the state
 * that logs its changes, then the tasks that use all of it.
 */
#include "bus.h"
#include "cli.h"
#include "clock.h"
#include "cocis_crypto.h"
#include "config.h"
#include "esp_log.h"
#include "esp_system.h"
#include "hub.h"
#include "i2c_bus.h"
#include "light_sensor.h"
#include "nvs_store.h"
#include "occupancy.h"
#include "room_node.h"
#include "storage.h"
#include "auth.h"
#include "users.h"

static const char *TAG = "main";

/* Opening-hours flag for the room nodes' energy baseline. */
static bool building_in_hours(void)
{
    sys_config_t cfg;
    struct tm t;
    config_get(&cfg);
    clock_local(&t);
    return schedule_is_open(&cfg.schedule, t.tm_wday, t.tm_hour * 60 + t.tm_min);
}

static const char *reset_reason_str(esp_reset_reason_t r)
{
    switch (r) {
    case ESP_RST_POWERON:  return "POWER_ON";
    case ESP_RST_SW:       return "SOFTWARE";
    case ESP_RST_PANIC:    return "PANIC";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:      return "WATCHDOG";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    default:               return "OTHER";
    }
}

void app_main(void)
{
    /* 1. Persistent storage and time */
    ESP_ERROR_CHECK(nvs_store_init());
    ESP_ERROR_CHECK(i2c_bus_init());
    if (clock_init() != ESP_OK) {
        ESP_LOGW(TAG, "RTC unavailable: time will not survive a reboot");
    }

    /* 2. Event log (SD gatekeeper) - everything after this can log */
    ESP_ERROR_CHECK(storage_init());
    if (!cocis_crypto_selftest()) {
        cocis_log(LOG_ALERT, "code=CRYPTO_FAIL detail=sha256_selftest");
    }

    /* 3. Shared state */
    ESP_ERROR_CHECK(users_init());
    ESP_ERROR_CHECK(config_init());
    ESP_ERROR_CHECK(occupancy_init());
    auth_init();

    /* 4. Communication and actuators */
    bus_init();
    ESP_ERROR_CHECK(light_sensor_init());

    /* 5. The three terminals */
    hub_start();
    for (int room = 1; room <= ROOM_COUNT; room++) {
        room_node_start(room, building_in_hours);
    }

    cocis_log(LOG_BOOT, "reason=%s", reset_reason_str(esp_reset_reason()));

    /* 6. Serial management menu (runs in its own task) */
    cli_start();
}
