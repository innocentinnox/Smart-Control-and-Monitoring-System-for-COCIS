/* Event log on the SD card, owned by ONE gatekeeper task.
 *
 * Any task calls cocis_log(); the record is queued and the gatekeeper is the
 * only code that ever touches the SD card / SPI bus. That gives:
 *   - no SPI or FATFS races between tasks (no mutex needed around the card),
 *   - one place that assigns sequence numbers (strictly increasing, no gaps),
 *   - callers never block on a slow card write.
 *
 * Each line is hash-chained to the previous one (docs/event-schema.md 2.1),
 * so `log verify` can prove the file has not been edited. One file per day:
 * /sdcard/LOGS/YYYYMMDD.LOG. Without a card the system keeps running and the
 * most recent lines are kept in a RAM ring buffer instead.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "cocis_types.h"
#include "esp_err.h"

#define LOG_FIELDS_MAX 160

/* Mounts the card (failure is not fatal) and starts the gatekeeper task. */
esp_err_t storage_init(void);
bool      storage_sd_ok(void);

/* Queue one event: fields are "k=v k=v" pairs, printf-style. Never blocks long. */
void cocis_log(log_type_t type, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

/* Print the last n lines of today's log to the console. Returns lines printed. */
int storage_tail(int n);

/* Re-check the hash chain of a day's file ("YYYYMMDD", NULL = today).
 * Returns lines checked, -1 if the file is missing; *bad_line = first broken
 * line number (1-based), or 0 if the chain is intact. */
int storage_verify(const char *date, int *bad_line);

/* Echo each new log line to the console (default on). */
void storage_set_echo(bool on);

uint32_t storage_last_seq(void);
