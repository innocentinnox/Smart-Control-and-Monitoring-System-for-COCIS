/* Same layering as the lab lcd_i2c driver, generalised to 4 rows:
 *   bus -> nibble (EN pulse) -> byte -> command/data -> rows. */
#include "lcd2004.h"

#include "board_pins.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2c_bus.h"

/* PCF8574 pins P0..P3 = RS, RW, EN, backlight; P4..P7 = D4..D7 */
#define PCF_RS  0x01
#define PCF_EN  0x04
#define PCF_BL  0x08

#define CMD_CLEAR        0x01
#define CMD_ENTRY_INC    0x06
#define CMD_DISPLAY_ON   0x0C
#define CMD_4BIT_2LINE   0x28
#define CMD_SET_DDRAM    0x80

/* DDRAM start address of each row on a 20x4 module */
static const uint8_t s_row_addr[LCD_ROWS] = { 0x00, 0x40, 0x14, 0x54 };

static i2c_master_dev_handle_t s_dev;

static void write_nibble(uint8_t nibble, uint8_t rs)
{
    uint8_t pins = (uint8_t)(nibble << 4) | PCF_BL | (rs ? PCF_RS : 0);
    uint8_t buf[2] = { pins | PCF_EN, pins };    /* EN high then low latches the nibble */
    i2c_master_transmit(s_dev, buf, sizeof(buf), 100);
    esp_rom_delay_us(50);
}

static void write_byte(uint8_t b, uint8_t rs)
{
    write_nibble(b >> 4, rs);
    write_nibble(b & 0x0F, rs);
}

static void command(uint8_t c) { write_byte(c, 0); }

esp_err_t lcd_init(void)
{
    esp_err_t err = i2c_bus_add_device(I2C_ADDR_LCD, &s_dev);
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(50));               /* power-on settle time */

    /* HD44780 "initialisation by instruction" into 4-bit mode */
    write_nibble(0x03, 0); vTaskDelay(pdMS_TO_TICKS(5));
    write_nibble(0x03, 0); esp_rom_delay_us(150);
    write_nibble(0x03, 0);
    write_nibble(0x02, 0);

    command(CMD_4BIT_2LINE);
    command(CMD_DISPLAY_ON);
    command(CMD_ENTRY_INC);
    lcd_clear();
    return ESP_OK;
}

void lcd_clear(void)
{
    command(CMD_CLEAR);
    vTaskDelay(pdMS_TO_TICKS(2));
}

void lcd_print_row(uint8_t row, const char *text)
{
    if (row >= LCD_ROWS) {
        return;
    }
    command(CMD_SET_DDRAM | s_row_addr[row]);
    for (int col = 0; col < LCD_COLS; col++) {
        char c = (text && *text) ? *text++ : ' ';
        write_byte((uint8_t)c, 1);
    }
}
