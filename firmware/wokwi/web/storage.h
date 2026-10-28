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
 *
 * Line format:   seq|timestamp|TYPE|k=v k=v ...|hash
 *   e.g.         000042|2026-10-28T09:15:03|ACCESS|uid=01020304 ...|5f1c...
 *   hash = hex SHA-256( previous line's hash + "|" + everything before the
 *          last '|' of this line ). The first line of each day file uses a
 *          "genesis" previous hash of 64 zeros.
 * Changing, deleting or inserting any line breaks the hash of that line (or
 * of the next one), and storage_verify() reports where.
 *
 * Gatekeeper pattern: instead of a lock around a shared resource, one task
 * owns it and everyone else sends it requests through a FreeRTOS queue.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "cocis_types.h"
#include "esp_err.h"

/* Max length of the "k=v ..." part of one record, including the NUL;
 * longer text is cut off. */
#define LOG_FIELDS_MAX 160

/* Mounts the card (failure is not fatal) and starts the gatekeeper task.
 * Call once at boot, before any other function in this file (cocis_log()
 * needs the queue created here). Without a card it logs an SD_FAIL alert
 * and carries on in RAM-only mode.
 * @return ESP_OK (also when the card is missing), ESP_ERR_NO_MEM if the
 *         request queue could not be created */
esp_err_t storage_init(void);

/* @return true if the SD card was mounted at boot (fixed after storage_init) */
bool      storage_sd_ok(void);

/* Queue one event: fields are "k=v k=v" pairs, printf-style. Never blocks long.
 *
 * The timestamp is taken here (when the event happened), not when the
 * gatekeeper later writes it. If the queue stays full for 100 ms the event
 * is dropped and a warning is printed, so a stuck card can never freeze the
 * caller.
 * @param type  event category (becomes the TYPE field)
 * @param fmt   printf format for the fields, e.g. "uid=%08lX side=%s"
 * @param ...   values for fmt
 * The format attribute lets the compiler check fmt against the arguments
 * like it does for printf. Call from any task, not from an ISR. */
void cocis_log(log_type_t type, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

/* Print the last n lines of today's log to the console. Returns lines printed.
 * Read from the card when possible (hashes hidden), else from the RAM ring.
 * Blocks until the gatekeeper has finished the job.
 * @param n  lines wanted; <= 0 means 10, values above 50 are capped at 50
 * @return number of lines printed (0 if there are none) */
int storage_tail(int n);

/* Re-check the hash chain of a day's file ("YYYYMMDD", NULL = today).
 * Returns lines checked, -1 if the file is missing; *bad_line = first broken
 * line number (1-based), or 0 if the chain is intact.
 * Blocks until the gatekeeper has read the whole file.
 * @param date      "YYYYMMDD" or NULL for today
 * @param bad_line  must not be NULL; set to 0 or the first bad line
 * @return lines read (up to and including the bad one), or -1 if the
 *         file cannot be opened or there is no SD card */
int storage_verify(const char *date, int *bad_line);

/* Echo each new log line to the console (default on).
 * @param on  false silences the "[LOG] ..." prints (lines are still stored) */
void storage_set_echo(bool on);

/* @return sequence number of the most recent log line (0 = none yet).
 * Numbering continues across reboots and days when a card is present. */
uint32_t storage_last_seq(void);
