/* DS1307 RTC driver (I2C address 0x68).
 *
 * The chip has 7 time registers starting at address 0x00:
 *   0x00 seconds (bit 7 = CH, "clock halt")   0x04 day of month 1..31
 *   0x01 minutes                              0x05 month 1..12
 *   0x02 hours (bit 6 = 12/24 h mode)         0x06 year 00..99
 *   0x03 day of week 1..7
 * All values are BCD (binary-coded decimal): each 4-bit half holds one
 * decimal digit, so 45 seconds is stored as 0x45, not 0x2D.
 * Reading: write the start register number, then read 7 bytes; the chip
 * auto-increments the register pointer after each byte.
 */
#include "ds1307.h"

#include "board_pins.h"
#include "i2c_bus.h"

#define REG_SECONDS     0x00        /* 7 time registers follow, all BCD */
#define BIT_CLOCK_HALT  0x80        /* in the seconds register */

static i2c_master_dev_handle_t s_dev;

/* BCD <-> normal numbers: 0x45 -> 4*10 + 5 = 45, and 45 -> (4<<4)|5 = 0x45 */
static uint8_t bcd_to_bin(uint8_t v) { return (uint8_t)((v >> 4) * 10 + (v & 0x0F)); }
static uint8_t bin_to_bcd(int v)     { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

esp_err_t ds1307_init(void)
{
    return i2c_bus_add_device(I2C_ADDR_RTC, &s_dev);
}

esp_err_t ds1307_read(struct tm *out)
{
    /* One combined transaction: send the register number (0x00), then read
     * all 7 time registers. 100 = timeout in ms. */
    uint8_t reg = REG_SECONDS, r[7];
    esp_err_t err = i2c_master_transmit_receive(s_dev, &reg, 1, r, sizeof(r), 100);
    if (err != ESP_OK) {
        return err;
    }
    /* CH set = oscillator stopped (fresh chip or battery removed): the
     * stored time is meaningless. */
    if (r[0] & BIT_CLOCK_HALT) {
        return ESP_ERR_INVALID_STATE;
    }
    /* Mask off the control bits before decoding (CH in seconds, mode bits
     * in hours) and shift to struct tm's ranges. */
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
    /* First byte = start register, then the 7 values in register order.
     * The hours byte has bit 6 = 0, which selects 24-hour mode. */
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
