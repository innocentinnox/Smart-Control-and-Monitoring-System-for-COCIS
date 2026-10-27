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

#define MOUNT_POINT  "/sdcard"
#define LOG_DIR      MOUNT_POINT "/LOGS"
#define LOG_LINE_MAX     320
#define RING_LINES   32         /* RAM copy of the newest lines */
#define TAIL_MAX     50
#define QUEUE_LEN    24
#define DATE_LEN     9          /* "YYYYMMDD" + NUL */

/* ---------------- Requests to the gatekeeper ---------------- */
typedef enum { REQ_APPEND, REQ_TAIL, REQ_VERIFY } req_kind_t;

typedef struct {
    req_kind_t kind;
    union {
        struct { log_type_t type; time_t ts; char fields[LOG_FIELDS_MAX]; } rec;
        struct { int n; } tail;
        struct { char date[DATE_LEN]; } verify;
    };
    SemaphoreHandle_t done;     /* given when a synchronous request finishes */
    int *result;
    int *aux;
} req_t;

static QueueHandle_t s_queue;
static bool          s_sd_ok;
static volatile bool s_echo = true;

/* ---------------- Gatekeeper-owned state (only that task touches it) ---------------- */
static FILE    *s_file;
static char     s_file_date[DATE_LEN];
static char     s_prev_hash[SHA256_HEX_LEN];
static volatile uint32_t s_seq;
static char     s_ring[RING_LINES][LOG_LINE_MAX];
static int      s_ring_head, s_ring_count;
static char     s_tail_buf[TAIL_MAX][LOG_LINE_MAX];
static char     s_line[LOG_LINE_MAX];

static const char GENESIS[SHA256_HEX_LEN] =
    "0000000000000000000000000000000000000000000000000000000000000000";

/* ================= Hash chain (shared by append and verify) ================= */

/* hash = SHA-256(prev_hash + "|" + content), hex encoded */
static void chain_hash(const char *prev_hex, const char *content, char out_hex[SHA256_HEX_LEN])
{
    char prefix[SHA256_HEX_LEN + 1];
    snprintf(prefix, sizeof(prefix), "%s|", prev_hex);

    uint8_t digest[SHA256_LEN];
    cocis_sha256(prefix, strlen(prefix), content, strlen(content), digest);
    cocis_hex(digest, SHA256_LEN, out_hex);
}

/* Split "content|hash" at the last '|'. Returns the hash, or NULL. */
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
static void day_path(const char *date, char *out, size_t len)
{
    snprintf(out, len, LOG_DIR "/%s.LOG", date);
}

static void today(char date[DATE_LEN])
{
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    strftime(date, DATE_LEN, "%Y%m%d", &tm);
}

/* Read a day file to its end and return the seq and hash of the last line. */
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
            *seq = (uint32_t)strtoul(s_line, NULL, 10);
            strcpy(hash, h);
            found = true;
        }
    }
    fclose(f);
    return found;
}

/* Switch the open file to the given day, continuing that file's chain. */
static bool open_day(const char *date)
{
    if (s_file && strcmp(date, s_file_date) == 0) {
        return true;
    }
    if (s_file) {
        fclose(s_file);
    }
    char path[48];
    day_path(date, path, sizeof(path));

    uint32_t seq;
    if (!read_last_entry(path, &seq, s_prev_hash)) {
        strcpy(s_prev_hash, GENESIS);           /* new day: new chain */
    }
    s_file = fopen(path, "a");
    strcpy(s_file_date, date);
    return s_file != NULL;
}

/* At boot: continue numbering after the newest line on the card. */
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
static void ring_push(const char *line)
{
    strlcpy(s_ring[s_ring_head], line, LOG_LINE_MAX);
    s_ring_head = (s_ring_head + 1) % RING_LINES;
    if (s_ring_count < RING_LINES) {
        s_ring_count++;
    }
}

static void handle_append(const req_t *r)
{
    struct tm tm;
    char ts[24], date[DATE_LEN], content[LOG_LINE_MAX - SHA256_HEX_LEN - 2];
    localtime_r(&r->rec.ts, &tm);
    strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%S", &tm);
    strftime(date, sizeof(date), "%Y%m%d", &tm);

    snprintf(content, sizeof(content), "%06lu|%s|%s|%s", (unsigned long)(s_seq + 1), ts,
             log_type_str(r->rec.type), r->rec.fields);

    bool on_card = s_sd_ok && open_day(date);
    if (!s_prev_hash[0]) {
        strcpy(s_prev_hash, GENESIS);           /* RAM-only mode: chain starts at boot */
    }
    char hash[SHA256_HEX_LEN];
    chain_hash(s_prev_hash, content, hash);

    if (on_card) {
        fprintf(s_file, "%s|%s\n", content, hash);
        fflush(s_file);
        fsync(fileno(s_file));                  /* survive a power cut */
    }
    s_seq++;
    strcpy(s_prev_hash, hash);
    ring_push(content);

    if (s_echo) {
        printf("[LOG] %s\n", content);
    }
}

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
        for (int i = count; i > 0; i--) {
            printf("%s\n", s_ring[(s_ring_head - i + RING_LINES) % RING_LINES]);
        }
        return count;
    }

    if (s_file) {
        fflush(s_file);
    }
    int total = 0;
    while (fgets(s_tail_buf[total % n], LOG_LINE_MAX, f)) {
        split_hash(s_tail_buf[total % n]);      /* hide the hash for readability */
        total++;
    }
    fclose(f);
    int count = total < n ? total : n;
    for (int i = total - count; i < total; i++) {
        printf("%s\n", s_tail_buf[i % n]);
    }
    return count;
}

static int handle_verify(const char *date, int *bad_line)
{
    char path[48];
    day_path(date, path, sizeof(path));
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
            *bad_line = n;
            break;
        }
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
        if (r.result) {
            *r.result = result;
        }
        if (r.done) {
            xSemaphoreGive(r.done);
        }
    }
}

/* ================= Public API (any task) ================= */

/* Send a request and wait for the gatekeeper to finish it. */
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
    req_t r = { .kind = REQ_APPEND };
    r.rec.type = type;
    r.rec.ts   = time(NULL);                    /* event time, not card-write time */

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

void     storage_set_echo(bool on) { s_echo = on; }
bool     storage_sd_ok(void)       { return s_sd_ok; }
uint32_t storage_last_seq(void)    { return s_seq; }

/* ================= Bring-up ================= */
static esp_err_t mount_card(void)
{
    spi_bus_config_t bus = {
        .mosi_io_num = PIN_SD_MOSI,
        .miso_io_num = PIN_SD_MISO,
        .sclk_io_num = PIN_SD_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    esp_err_t err = spi_bus_initialize(host.slot, &bus, SDSPI_DEFAULT_DMA);
    if (err != ESP_OK) {
        return err;
    }

    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.gpio_cs = PIN_SD_CS;
    slot.host_id = host.slot;

    esp_vfs_fat_sdmmc_mount_config_t mount = {
        .format_if_mount_failed = true,         /* a blank simulated card gets formatted */
        .max_files = 4,
        .allocation_unit_size = 16 * 1024,
    };
    sdmmc_card_t *card;
    err = esp_vfs_fat_sdspi_mount(MOUNT_POINT, &host, &slot, &mount, &card);
    if (err == ESP_OK) {
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

    esp_err_t err = mount_card();
    s_sd_ok = err == ESP_OK;
    if (s_sd_ok) {
        ESP_LOGI(TAG, "SD card mounted at " MOUNT_POINT);
        recover_seq();
    } else {
        ESP_LOGW(TAG, "SD card unavailable (%s): logging to RAM only", esp_err_to_name(err));
    }

    xTaskCreate(storage_task, "storage", TASK_STORAGE_STACK, NULL, TASK_STORAGE_PRIO, NULL);
    if (!s_sd_ok) {
        cocis_log(LOG_ALERT, "code=SD_FAIL detail=%s", esp_err_to_name(err));
    }
    return ESP_OK;
}
