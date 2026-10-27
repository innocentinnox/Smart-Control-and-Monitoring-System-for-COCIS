#include "ds1307.h"

#include "board_pins.h"
#include "i2c_bus.h"

#define REG_SECONDS     0x00        /* 7 time registers follow, all BCD */
#define BIT_CLOCK_HALT  0x80        /* in the seconds register */

static i2c_master_dev_handle_t s_dev;

static uint8_t bcd_to_bin(uint8_t v) { return (uint8_t)((v >> 4) * 10 + (v & 0x0F)); }
static uint8_t bin_to_bcd(int v)     { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

esp_err_t ds1307_init(void)
{
    return i2c_bus_add_device(I2C_ADDR_RTC, &s_dev);
}

esp_err_t ds1307_read(struct tm *out)
{
    uint8_t reg = REG_SECONDS, r[7];
    esp_err_t err = i2c_master_transmit_receive(s_dev, &reg, 1, r, sizeof(r), 100);
    if (err != ESP_OK) {
        return err;
    }
    if (r[0] & BIT_CLOCK_HALT) {
        return ESP_ERR_INVALID_STATE;
    }
    *out = (struct tm){
        .tm_sec  = bcd_to_bin(r[0] & 0x7F),
        .tm_min  = bcd_to_bin(r[1]),
        .tm_hour = bcd_to_bin(r[2] & 0x3F),         /* 24-hour mode */
        .tm_wday = bcd_to_bin(r[3]) - 1,            /* RTC: 1..7, tm: 0..6 */
        .tm_mday = bcd_to_bin(r[4]),
        .tm_mon  = bcd_to_bin(r[5]) - 1,
        .tm_year = bcd_to_bin(r[6]) + 100,          /* years since 1900, RTC holds 20xx */
        .tm_isdst = -1,
    };
    return ESP_OK;
}

esp_err_t ds1307_write(const struct tm *t)
{
    uint8_t buf[8] = {
        REG_SECONDS,
        bin_to_bcd(t->tm_sec),                      /* also clears CLOCK_HALT */
        bin_to_bcd(t->tm_min),
        bin_to_bcd(t->tm_hour),
        bin_to_bcd(t->tm_wday + 1),
        bin_to_bcd(t->tm_mday),
        bin_to_bcd(t->tm_mon + 1),
        bin_to_bcd(t->tm_year % 100),
    };
    return i2c_master_transmit(s_dev, buf, sizeof(buf), 100);
}
