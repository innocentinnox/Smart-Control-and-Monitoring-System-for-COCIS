/* CoCIS Smart Control & Monitoring System - boot sequence.
 *
 * app_main only wires the system together; every behaviour lives in a
 * component (see docs/report.pdf for the architecture):
 *
 *   common      shared types, pin map, task table, SHA-256
 *   core_logic  pure rules: access policy, room lighting, admin 2FA (host-tested)
 *   drivers     I2C/LCD/RTC/MFRC522 RFID/buttons/PWM/ADC
 *   bus         message passing between the three terminals
 *   storage     SD-card gatekeeper task + hash-chained event log
 *   services    users, config, occupancy ledger, auth, clock (mutex + NVS)
 *   ui          LCD + buzzer + LED task
 *   nodes       entrance hub, room nodes, door, RFID readers
 *   cli         serial management menu
 *
 * Order matters: time first (log timestamps), then the log, then the state
 * that logs its changes, then the tasks that use all of it.
 *
 * app_main() is called by ESP-IDF in its own "main" task once FreeRTOS is
 * running. It only initialises and starts the other tasks, then returns;
 * the system keeps running in those tasks.
 */
#include "bus.h"
#include "cli.h"
#include "clock.h"
#include "cocis_crypto.h"
#include "config.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hub.h"
#include "i2c_bus.h"
#include "light_sensor.h"
#include "nvs_store.h"
#include "occupancy.h"
#include "room_node.h"
#include "storage.h"
#include "task_config.h"
#include "auth.h"
#include "users.h"

static const char *TAG = "main";

/* Opening-hours flag for the room nodes' energy baseline.
 * Passed to room_node_start() and called from the room tasks every 200 ms:
 * true if the building schedule says "open" at the current local time. */
static bool building_in_hours(void)
{
    sys_config_t cfg;
    struct tm t;
    config_get(&cfg);
    clock_local(&t);
    return schedule_is_open(&cfg.schedule, t.tm_wday, t.tm_hour * 60 + t.tm_min);
}

/* Turn the ESP-IDF reset reason into a short word for the BOOT log record.
 * Lets the log show WHY the board restarted (e.g. a crash or a watchdog),
 * not only that it did. All three watchdog kinds are reported as one. */
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

/* The whole bring-up, run in its own task so its stack size is ours to
 * choose (TASK_BOOT_STACK) rather than the main task's, which is only 3.5 KB
 * in a default ESP-IDF build such as the wokwi.com online builder.
 * @param arg  unused */
static void boot_task(void *arg)
{
    /* ESP_ERROR_CHECK(): if a call fails, print the error and reboot. Used
     * for steps the system cannot work without. Optional parts (RTC, SD
     * card, crypto self-test) only warn or log, and boot carries on. */

    /* 1. Persistent storage and time.
     * NVS (flash key-value store) first: users, config and occupancy are
     * loaded from it later. I2C next because the RTC is on the I2C bus;
     * then the clock, so every log record from here on has a real time. */
    ESP_ERROR_CHECK(nvs_store_init());
    ESP_ERROR_CHECK(i2c_bus_init());
    if (clock_init() != ESP_OK) {
        ESP_LOGW(TAG, "RTC unavailable: time will not survive a reboot");
    }

    /* 2. Event log (SD gatekeeper) - everything after this can log.
     * The SHA-256 self-test runs once the log exists, so a failure can be
     * recorded; the hash chain of the log depends on SHA-256 being right. */
    ESP_ERROR_CHECK(storage_init());
    if (!cocis_crypto_selftest()) {
        cocis_log(LOG_ALERT, "code=CRYPTO_FAIL detail=sha256_selftest");
    }

    /* 3. Shared state.
     * Loaded from NVS (or seeded with defaults on first boot). auth comes
     * after users because checking an admin card/PIN looks up users. */
    ESP_ERROR_CHECK(users_init());
    ESP_ERROR_CHECK(config_init());
    ESP_ERROR_CHECK(occupancy_init());
    auth_init();

    /* 4. Communication and actuators.
     * The bus inboxes must exist before any task sends or receives, and
     * the shared ADC before the room nodes add their LDRs. */
    bus_init();
    ESP_ERROR_CHECK(light_sensor_init());

    /* 5. The three terminals.
     * The hub (entrance) and the two lecture-room nodes, each its own
     * FreeRTOS task. They only talk through the bus, as separate boards
     * would. */
    hub_start();
    for (int room = 1; room <= ROOM_COUNT; room++) {
        room_node_start(room, building_in_hours);
    }

    /* Boot record (with the reset reason) once everything is up. */
    cocis_log(LOG_BOOT, "reason=%s", reset_reason_str(esp_reset_reason()));

    /* 6. Serial management menu (runs in its own task) */
    cli_start();

    /* Bring-up done; every component now runs in its own task. */
    vTaskDelete(NULL);
}

void app_main(void)
{
    xTaskCreate(boot_task, "boot", TASK_BOOT_STACK, NULL, TASK_BOOT_PRIO, NULL);
}
