/* Time keeping (see clock.h).
 *
 * The ESP32 system clock (time(), gettimeofday()) starts at 1970 on every
 * boot. At boot we copy the DS1307 battery-backed RTC into it; when the
 * time is set we write both. The RTC holds LOCAL time, so conversions go
 * through mktime()/localtime_r() with TZ set to EAT.
 */
#include "clock.h"

#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "ds1307.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "clock";

/* POSIX TZ string: name EAT, 3 hours east of UTC, no daylight saving.
 * (POSIX counts the offset the "wrong" way: -3 means UTC+3.) */
#define TIMEZONE "EAT-3"

/* Set the system clock from a local broken-down time.
 * mktime() treats the struct as local time and returns UTC seconds, which is
 * what settimeofday() needs. A copy is used because mktime() modifies its
 * argument. */
static void set_system_time(const struct tm *local)
{
    struct tm copy = *local;
    struct timeval tv = { .tv_sec = mktime(&copy) };     /* mktime applies the TZ */
    settimeofday(&tv, NULL);
}

/* Fallback time = when this firmware was compiled. The compiler provides
 * the date and time as text macros, which are parsed here:
 * __DATE__ = "Oct 27 2026", __TIME__ = "09:15:02" */
static void build_time(struct tm *out)
{
    /* Month number = position of the 3-letter name in this string / 3. */
    static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    char mon[4] = { 0 };
    int day, year, h, m, s;
    sscanf(__DATE__, "%3s %d %d", mon, &day, &year);
    sscanf(__TIME__, "%d:%d:%d", &h, &m, &s);
    /* struct tm counts years from 1900 and months from 0. tm_isdst = -1
     * means "work out daylight saving yourself" (none in EAT). */
    *out = (struct tm){
        .tm_year = year - 1900, .tm_mon = (int)((strstr(months, mon) - months) / 3),
        .tm_mday = day, .tm_hour = h, .tm_min = m, .tm_sec = s, .tm_isdst = -1,
    };
    mktime(out);                                         /* fills in tm_wday */
}

esp_err_t clock_init(void)
{
    /* Set the timezone first so every later mktime()/localtime_r() uses EAT. */
    setenv("TZ", TIMEZONE, 1);
    tzset();

    /* Use the RTC only if it initialised, could be read, and holds a
     * plausible date (tm_year >= 124 means year 2024 or later; anything
     * earlier means the RTC was reset or never set). */
    esp_err_t err = ds1307_init();
    struct tm t;
    if (err == ESP_OK && (err = ds1307_read(&t)) == ESP_OK && t.tm_year >= 124) {
        set_system_time(&t);
        ESP_LOGI(TAG, "Time from RTC: %04d-%02d-%02d %02d:%02d", t.tm_year + 1900,
                 t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min);
        return ESP_OK;
    }

    /* No usable RTC time: the build time is at least close to "now", and
     * writing it to the RTC means the next boot finds a set clock. */
    ESP_LOGW(TAG, "RTC not set (%s): using build time", esp_err_to_name(err));
    build_time(&t);
    return clock_set(&t);
}

esp_err_t clock_set(const struct tm *local)
{
    set_system_time(local);
    /* Read the time back from the system clock: this gives a normalised
     * struct (e.g. correct weekday) to store in the RTC. */
    struct tm normalised;
    clock_local(&normalised);                            /* valid tm_wday for the RTC */
    return ds1307_write(&normalised);
}

void clock_local(struct tm *out)
{
    /* localtime_r is the thread-safe version of localtime (it writes into
     * the caller's struct instead of a shared static one). */
    time_t now = time(NULL);
    localtime_r(&now, out);
}

uint32_t clock_ms(void)
{
    /* esp_timer_get_time() = microseconds since boot (64-bit, monotonic). */
    return (uint32_t)(esp_timer_get_time() / 1000);
}

void clock_format(char *out, size_t len, const char *fmt)
{
    struct tm t;
    clock_local(&t);
    strftime(out, len, fmt, &t);
}
