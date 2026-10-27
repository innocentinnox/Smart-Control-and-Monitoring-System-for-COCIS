#include "clock.h"

#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "ds1307.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "clock";

/* POSIX TZ string: name EAT, 3 hours east of UTC, no daylight saving. */
#define TIMEZONE "EAT-3"

static void set_system_time(const struct tm *local)
{
    struct tm copy = *local;
    struct timeval tv = { .tv_sec = mktime(&copy) };     /* mktime applies the TZ */
    settimeofday(&tv, NULL);
}

/* __DATE__ = "Oct 27 2026", __TIME__ = "09:15:02" */
static void build_time(struct tm *out)
{
    static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    char mon[4] = { 0 };
    int day, year, h, m, s;
    sscanf(__DATE__, "%3s %d %d", mon, &day, &year);
    sscanf(__TIME__, "%d:%d:%d", &h, &m, &s);
    *out = (struct tm){
        .tm_year = year - 1900, .tm_mon = (int)((strstr(months, mon) - months) / 3),
        .tm_mday = day, .tm_hour = h, .tm_min = m, .tm_sec = s, .tm_isdst = -1,
    };
    mktime(out);                                         /* fills in tm_wday */
}

esp_err_t clock_init(void)
{
    setenv("TZ", TIMEZONE, 1);
    tzset();

    esp_err_t err = ds1307_init();
    struct tm t;
    if (err == ESP_OK && (err = ds1307_read(&t)) == ESP_OK && t.tm_year >= 124) {
        set_system_time(&t);
        ESP_LOGI(TAG, "Time from RTC: %04d-%02d-%02d %02d:%02d", t.tm_year + 1900,
                 t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min);
        return ESP_OK;
    }

    ESP_LOGW(TAG, "RTC not set (%s): using build time", esp_err_to_name(err));
    build_time(&t);
    return clock_set(&t);
}

esp_err_t clock_set(const struct tm *local)
{
    set_system_time(local);
    struct tm normalised;
    clock_local(&normalised);                            /* valid tm_wday for the RTC */
    return ds1307_write(&normalised);
}

void clock_local(struct tm *out)
{
    time_t now = time(NULL);
    localtime_r(&now, out);
}

uint32_t clock_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

void clock_format(char *out, size_t len, const char *fmt)
{
    struct tm t;
    clock_local(&t);
    strftime(out, len, fmt, &t);
}
