/* SD-card event log: the storage gatekeeper task (see storage.h).
 *
 * Flow:  any task --cocis_log()--> s_queue --> storage_task --> SD card
 *                                                          \--> RAM ring
 * Only storage_task (and storage_init before the task starts) ever calls
 * the file functions, so the card, the open FILE and the hash chain need
 * no locking.
 *
 * Sync requests (tail, verify) carry a binary semaphore: the caller sleeps
 * on it and the gatekeeper "gives" it when the job is done. A binary
 * semaphore is a FreeRTOS signal flag: Take waits until someone Gives.
 */
#include "storage.h"

#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "board_pins.h"
#include "cocis_crypto.h"
#include "driver/sdspi_host.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "sdmmc_cmd.h"
#include "task_config.h"

static const char *TAG = "storage";

/* The card's FAT filesystem appears under this path, so plain fopen()/
 * fprintf() work on it (ESP-IDF "VFS" layer). */
#define MOUNT_POINT  "/sdcard"
#define LOG_DIR      MOUNT_POINT "/LOGS"
#define LOG_LINE_MAX     320    /* one full line incl. hash, '\n' and NUL */
#define RING_LINES   32         /* RAM copy of the newest lines */
#define TAIL_MAX     50         /* most lines storage_tail() will print */
#define QUEUE_LEN    24         /* requests that can wait for the gatekeeper */
#define DATE_LEN     9          /* "YYYYMMDD" + NUL */

/* ---------------- Requests to the gatekeeper ---------------- */
typedef enum { REQ_APPEND, REQ_TAIL, REQ_VERIFY } req_kind_t;

/* One job for the gatekeeper; copied whole into the queue. `kind` says
 * which union member is used. For async appends done/result/aux are NULL. */
typedef struct {
    req_kind_t kind;
    union {
        /* REQ_APPEND: event type, event time (seconds since 1970), "k=v" text */
        struct { log_type_t type; time_t ts; char fields[LOG_FIELDS_MAX]; } rec;
        /* REQ_TAIL: how many lines to print */
        struct { int n; } tail;
        /* REQ_VERIFY: which day file to check, "YYYYMMDD" */
        struct { char date[DATE_LEN]; } verify;
    };
    SemaphoreHandle_t done;     /* given when a synchronous request finishes */
    /* where the gatekeeper stores the handler's return value (caller's variable) */
    int *result;
    /* extra output: REQ_VERIFY writes the first bad line number here */
    int *aux;
} req_t;

static QueueHandle_t s_queue;       /* requests from all tasks to the gatekeeper */
static bool          s_sd_ok;       /* card mounted at boot; set before the task starts */
static volatile bool s_echo = true; /* volatile: set by the CLI task, read by the gatekeeper */

/* ---------------- Gatekeeper-owned state (only that task touches it) ---------------- */
static FILE    *s_file;                         /* today's log file, open for append (or NULL) */
static char     s_file_date[DATE_LEN];          /* which day s_file belongs to */
static char     s_prev_hash[SHA256_HEX_LEN];    /* hash of the last line written: next link of the chain */
/* last sequence number used; volatile because storage_last_seq() reads it from other tasks */
static volatile uint32_t s_seq;
static char     s_ring[RING_LINES][LOG_LINE_MAX];   /* circular buffer of the newest lines (no hash) */
static int      s_ring_head, s_ring_count;      /* next slot to write / lines stored (max RING_LINES) */
static char     s_tail_buf[TAIL_MAX][LOG_LINE_MAX]; /* scratch for storage_tail(); static: too big for a stack */
static char     s_line[LOG_LINE_MAX];           /* scratch line for reading files */

/* "Previous hash" of the first line of every day file: a fixed, known
 * start so a verifier can recompute the chain from line 1. */
static const char GENESIS[SHA256_HEX_LEN] =
    "0000000000000000000000000000000000000000000000000000000000000000";

/* ================= Hash chain (shared by append and verify) ================= */

/* hash = SHA-256(prev_hash + "|" + content), hex encoded
 * Including the previous line's hash is what links the lines: editing any
 * line changes its hash, which no longer matches what the next line was
 * built from. Append and verify must both use this one function so they
 * can never disagree on the exact bytes that are hashed.
 * @param prev_hex  64-char hex hash of the previous line (or GENESIS)
 * @param content   the line without its "|hash" suffix and without '\n'
 * @param out_hex   receives the 64-char hex hash + NUL */
static void chain_hash(const char *prev_hex, const char *content, char out_hex[SHA256_HEX_LEN])
{
    /* 64 hex chars + '|' + NUL */
    char prefix[SHA256_HEX_LEN + 1];
    snprintf(prefix, sizeof(prefix), "%s|", prev_hex);

    uint8_t digest[SHA256_LEN];
    cocis_sha256(prefix, strlen(prefix), content, strlen(content), digest);
    cocis_hex(digest, SHA256_LEN, out_hex);
}

/* Split "content|hash" at the last '|'. Returns the hash, or NULL.
 * Works in place: strips the line ending, then puts a NUL where the last
 * '|' was, so afterwards `line` is just the content. The last '|' is used
 * because the content itself contains '|' separators. A line whose last
 * field is not exactly 64 chars long is not a valid log line (NULL, and
 * the line is left unsplit). */
static char *split_hash(char *line)
{
    line[strcspn(line, "\r\n")] = '\0';
    char *bar = strrchr(line, '|');
    if (!bar || strlen(bar + 1) != SHA256_HEX_LEN - 1) {
        return NULL;
    }
    *bar = '\0';
    return bar + 1;
}

/* ================= File helpers ================= */

/* "20261028" -> "/sdcard/LOGS/20261028.LOG". The 8.3 upper-case name
 * suits FAT without long-file-name support. */
static void day_path(const char *date, char *out, size_t len)
{
    snprintf(out, len, LOG_DIR "/%s.LOG", date);
}

/* Today's local date as "YYYYMMDD" (from the system clock, set by the RTC). */
static void today(char date[DATE_LEN])
{
    time_t now = time(NULL);
    struct tm tm;
    /* localtime_r is the thread-safe version of localtime (no shared buffer) */
    localtime_r(&now, &tm);
    strftime(date, DATE_LEN, "%Y%m%d", &tm);
}

/* Read a day file to its end and return the seq and hash of the last line.
 * Lines without a valid hash are skipped, so a half-written last line (e.g.
 * after a power cut) does not hide the last good one.
 * @return false if the file is missing or has no valid line (outputs untouched)
 * Uses the shared s_line buffer: gatekeeper / boot only. */
static bool read_last_entry(const char *path, uint32_t *seq, char hash[SHA256_HEX_LEN])
{
    FILE *f = fopen(path, "r");
    if (!f) {
        return false;
    }
    bool found = false;
    while (fgets(s_line, sizeof(s_line), f)) {
        char *h = split_hash(s_line);
        if (h) {
            /* the line starts with the seq ("000042|..."); strtoul stops at the '|' */
            *seq = (uint32_t)strtoul(s_line, NULL, 10);
            strcpy(hash, h);
            found = true;
        }
    }
    fclose(f);
    return found;
}

/* Switch the open file to the given day, continuing that file's chain.
 * If the file already has lines (e.g. after a reboot the same day), the
 * chain continues from its last hash; a new file starts from GENESIS.
 * @param date  "YYYYMMDD" of the record being written
 * @return true if the file is open for appending */
static bool open_day(const char *date)
{
    /* fast path: already writing this day's file */
    if (s_file && strcmp(date, s_file_date) == 0) {
        return true;
    }
    if (s_file) {
        fclose(s_file);
    }
    char path[48];
    day_path(date, path, sizeof(path));

    /* seq from the file is not needed here (s_seq is global and already
     * correct), only the last hash */
    uint32_t seq;
    if (!read_last_entry(path, &seq, s_prev_hash)) {
        strcpy(s_prev_hash, GENESIS);           /* new day: new chain */
    }
    s_file = fopen(path, "a");
    strcpy(s_file_date, date);
    return s_file != NULL;
}

/* At boot: continue numbering after the newest line on the card.
 * "Newest" = the largest file name: YYYYMMDD names sort by date when
 * compared as plain strings. */
static void recover_seq(void)
{
    DIR *dir = opendir(LOG_DIR);
    if (!dir) {
        return;
    }
    char newest[32] = "";
    struct dirent *e;
    while ((e = readdir(dir)) != NULL) {
        if (strstr(e->d_name, ".LOG") && strcmp(e->d_name, newest) > 0) {
            strlcpy(newest, e->d_name, sizeof(newest));
        }
    }
    closedir(dir);

    if (newest[0]) {
        char path[48], hash[SHA256_HEX_LEN];
        uint32_t seq = 0;
        snprintf(path, sizeof(path), LOG_DIR "/%s", newest);
        if (read_last_entry(path, &seq, hash)) {
            s_seq = seq;
            ESP_LOGI(TAG, "Resuming log after seq %lu (%s)", (unsigned long)seq, newest);
        }
    }
}

/* ================= Request handlers (gatekeeper task) ================= */

/* Store a line in the RAM ring. s_ring_head is the slot written next; once
 * the ring is full it overwrites the oldest line (head wraps with %). */
static void ring_push(const char *line)
{
    strlcpy(s_ring[s_ring_head], line, LOG_LINE_MAX);
    s_ring_head = (s_ring_head + 1) % RING_LINES;
    if (s_ring_count < RING_LINES) {
        s_ring_count++;
    }
}

/* Build, hash and write one log line (REQ_APPEND).
 * The record goes into the file of the day of its OWN timestamp, so an
 * event queued just before midnight still lands in that day's file. */
static void handle_append(const req_t *r)
{
    struct tm tm;
    /* content leaves room for "|" + 64-char hash + NUL within LOG_LINE_MAX */
    char ts[24], date[DATE_LEN], content[LOG_LINE_MAX - SHA256_HEX_LEN - 2];
    localtime_r(&r->rec.ts, &tm);
    strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%S", &tm);   /* ISO 8601, e.g. 2026-10-28T09:15:03 */
    strftime(date, sizeof(date), "%Y%m%d", &tm);

    /* "seq|ts|TYPE|fields"; seq zero-padded to 6 digits so lines line up */
    snprintf(content, sizeof(content), "%06lu|%s|%s|%s", (unsigned long)(s_seq + 1), ts,
             log_type_str(r->rec.type), r->rec.fields);

    /* open_day() also loads s_prev_hash from the file when switching days */
    bool on_card = s_sd_ok && open_day(date);
    if (!s_prev_hash[0]) {
        strcpy(s_prev_hash, GENESIS);           /* RAM-only mode: chain starts at boot */
    }
    char hash[SHA256_HEX_LEN];
    chain_hash(s_prev_hash, content, hash);

    if (on_card) {
        fprintf(s_file, "%s|%s\n", content, hash);
        /* fflush moves the C library buffer into FATFS; fsync makes FATFS
         * write it (and the directory entry) to the card itself */
        fflush(s_file);
        fsync(fileno(s_file));                  /* survive a power cut */
    }
    /* Advance seq and chain even if the card write was skipped, so the RAM
     * copy and the console echo stay numbered consistently. */
    s_seq++;
    strcpy(s_prev_hash, hash);
    ring_push(content);

    if (s_echo) {
        printf("[LOG] %s\n", content);
    }
}

/* Print the last n lines of today's file (REQ_TAIL); falls back to the RAM
 * ring if there is no card or no file for today yet.
 * @return number of lines printed */
static int handle_tail(int n)
{
    if (n <= 0) n = 10;
    if (n > TAIL_MAX) n = TAIL_MAX;

    char date[DATE_LEN], path[48];
    today(date);
    day_path(date, path, sizeof(path));
    FILE *f = s_sd_ok ? fopen(path, "r") : NULL;

    if (!f) {                                   /* no card: serve from the RAM ring */
        int count = n < s_ring_count ? n : s_ring_count;
        /* oldest first: start `count` slots behind head; + RING_LINES keeps
         * the index positive before the % */
        for (int i = count; i > 0; i--) {
            printf("%s\n", s_ring[(s_ring_head - i + RING_LINES) % RING_LINES]);
        }
        return count;
    }

    /* make sure lines still in the C library buffer are in the file we read */
    if (s_file) {
        fflush(s_file);
    }
    /* Read the whole file, keeping only the last n lines: line k goes into
     * slot k % n, so older lines are overwritten as we go. */
    int total = 0;
    while (fgets(s_tail_buf[total % n], LOG_LINE_MAX, f)) {
        split_hash(s_tail_buf[total % n]);      /* hide the hash for readability */
        total++;
    }
    fclose(f);
    /* print the kept lines in file order (oldest of the last n first) */
    int count = total < n ? total : n;
    for (int i = total - count; i < total; i++) {
        printf("%s\n", s_tail_buf[i % n]);
    }
    return count;
}

/* Recompute the chain of one day file from GENESIS and compare each line's
 * stored hash (REQ_VERIFY). Stops at the first mismatch.
 * @param date      "YYYYMMDD"
 * @param bad_line  set to the first bad line (1-based), 0 if all good
 * @return lines read, or -1 if the file cannot be opened */
static int handle_verify(const char *date, int *bad_line)
{
    char path[48];
    day_path(date, path, sizeof(path));
    /* include lines that are still buffered for the file being written */
    if (s_file) {
        fflush(s_file);
    }
    FILE *f = s_sd_ok ? fopen(path, "r") : NULL;
    if (!f) {
        return -1;
    }
    char prev[SHA256_HEX_LEN], calc[SHA256_HEX_LEN];
    strcpy(prev, GENESIS);
    int n = 0;
    *bad_line = 0;
    while (fgets(s_line, sizeof(s_line), f)) {
        n++;
        char *stored = split_hash(s_line);
        if (!stored) {
            /* no hash at all: damaged or hand-written line */
            *bad_line = n;
            break;
        }
        /* s_line is now only the content part (split_hash cut it) */
        chain_hash(prev, s_line, calc);
        if (strcmp(calc, stored) != 0) {
            *bad_line = n;
            break;
        }
        strcpy(prev, stored);
    }
    fclose(f);
    return n;
}

/* The gatekeeper task: handles one request at a time, forever, in the
 * order they were queued. Sleeps (no CPU) while the queue is empty. */
static void storage_task(void *arg)
{
    static req_t r;                             /* large: keep it off the stack */
    while (1) {
        xQueueReceive(s_queue, &r, portMAX_DELAY);
        int result = 0;
        switch (r.kind) {
        case REQ_APPEND: handle_append(&r); break;
        case REQ_TAIL:   result = handle_tail(r.tail.n); break;
        case REQ_VERIFY: result = handle_verify(r.verify.date, r.aux); break;
        }
        /* for sync requests: hand the answer back, then wake the caller */
        if (r.result) {
            *r.result = result;
        }
        if (r.done) {
            xSemaphoreGive(r.done);
        }
    }
}

/* ================= Public API (any task) ================= */

/* Send a request and wait for the gatekeeper to finish it.
 * `result` lives on this function's stack; that is safe only because we
 * do not return until the gatekeeper has written it and given `done`.
 * Must not be called from the gatekeeper task itself (it would wait for
 * itself forever). */
static int request_sync(req_t *r)
{
    int result = 0;
    r->done   = xSemaphoreCreateBinary();
    r->result = &result;
    xQueueSend(s_queue, r, portMAX_DELAY);
    xSemaphoreTake(r->done, portMAX_DELAY);
    vSemaphoreDelete(r->done);
    return result;
}

void cocis_log(log_type_t type, const char *fmt, ...)
{
    /* designated initializer: every other field (done, result, aux) is NULL,
     * which marks this as a fire-and-forget request */
    req_t r = { .kind = REQ_APPEND };
    r.rec.type = type;
    r.rec.ts   = time(NULL);                    /* event time, not card-write time */

    /* format the caller's "k=v" text now, while its arguments still exist */
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(r.rec.fields, sizeof(r.rec.fields), fmt, ap);
    va_end(ap);

    if (xQueueSend(s_queue, &r, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "log queue full, %s event lost", log_type_str(type));
    }
}

int storage_tail(int n)
{
    req_t r = { .kind = REQ_TAIL, .tail.n = n };
    return request_sync(&r);
}

int storage_verify(const char *date, int *bad_line)
{
    req_t r = { .kind = REQ_VERIFY, .aux = bad_line };
    if (date) {
        strlcpy(r.verify.date, date, DATE_LEN);
    } else {
        today(r.verify.date);
    }
    return request_sync(&r);
}

/* Tiny accessors: single-word reads/writes, safe from any task. */
void     storage_set_echo(bool on) { s_echo = on; }
bool     storage_sd_ok(void)       { return s_sd_ok; }
uint32_t storage_last_seq(void)    { return s_seq; }

/* ================= Bring-up ================= */

/* Start the SPI bus and mount the card's FAT filesystem at MOUNT_POINT,
 * then make sure the LOGS folder exists.
 * @return ESP_OK, or the ESP-IDF error (no card, wiring, bus init...) */
static esp_err_t mount_card(void)
{
    /* plain SPI: one data line each way; quad-SPI pins unused (-1) */
    spi_bus_config_t bus = {
        .mosi_io_num = PIN_SD_MOSI,
        .miso_io_num = PIN_SD_MISO,
        .sclk_io_num = PIN_SD_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,                /* bytes per SPI transaction (DMA buffer size) */
    };
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    esp_err_t err = spi_bus_initialize(host.slot, &bus, SDSPI_DEFAULT_DMA);
    if (err != ESP_OK) {
        return err;
    }

    /* the card itself is one device on that bus, selected by its CS pin */
    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.gpio_cs = PIN_SD_CS;
    slot.host_id = host.slot;

    esp_vfs_fat_sdmmc_mount_config_t mount = {
        .format_if_mount_failed = true,         /* a blank simulated card gets formatted */
        .max_files = 4,                         /* files open at the same time */
        .allocation_unit_size = 16 * 1024,      /* FAT cluster size used when formatting */
    };
    sdmmc_card_t *card;
    err = esp_vfs_fat_sdspi_mount(MOUNT_POINT, &host, &slot, &mount, &card);
    if (err == ESP_OK) {
        /* fails harmlessly if the folder already exists */
        mkdir(LOG_DIR, 0775);
    }
    return err;
}

esp_err_t storage_init(void)
{
    s_queue = xQueueCreate(QUEUE_LEN, sizeof(req_t));
    if (!s_queue) {
        return ESP_ERR_NO_MEM;
    }

    /* Done here, before the gatekeeper exists, so nothing else can be
     * using the card yet. */
    esp_err_t err = mount_card();
    s_sd_ok = err == ESP_OK;
    if (s_sd_ok) {
        ESP_LOGI(TAG, "SD card mounted at " MOUNT_POINT);
        recover_seq();
    } else {
        ESP_LOGW(TAG, "SD card unavailable (%s): logging to RAM only", esp_err_to_name(err));
    }

    xTaskCreate(storage_task, "storage", TASK_STORAGE_STACK, NULL, TASK_STORAGE_PRIO, NULL);
    /* logged through the normal path, so the alert also appears in the RAM log */
    if (!s_sd_ok) {
        cocis_log(LOG_ALERT, "code=SD_FAIL detail=%s", esp_err_to_name(err));
    }
    return ESP_OK;
}
