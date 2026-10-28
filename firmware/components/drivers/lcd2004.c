/* Same layering as the lab lcd_i2c driver, generalised to 4 rows:
 *   bus -> nibble (EN pulse) -> byte -> command/data -> rows.
 *
 * How a byte reaches the LCD: the PCF8574 backpack (I2C 0x27) has 8 output
 * pins. Only 4 of them are LCD data lines (D4..D7), so the HD44780 runs in
 * 4-bit mode and every byte is sent as two halves ("nibbles"), high half
 * first. Each nibble is latched by the LCD on the falling edge of EN, so we
 * write the nibble twice over I2C: once with EN=1, once with EN=0.
 *
 * RS selects what the byte means: RS=0 -> a command (clear, move cursor),
 * RS=1 -> a character to display. RW is kept 0 (we only ever write).
 */
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

/* HD44780 commands used here */
#define CMD_CLEAR        0x01   /* blank screen, cursor home (slow: ~1.5 ms) */
#define CMD_ENTRY_INC    0x06   /* cursor moves right after each character  */
#define CMD_DISPLAY_ON   0x0C   /* display on, cursor off, blink off         */
#define CMD_4BIT_2LINE   0x28   /* 4-bit interface, 2-line mode, 5x8 font    */
#define CMD_SET_DDRAM    0x80   /* OR with an address to move the cursor     */

/* DDRAM start address of each row on a 20x4 module.
 * The order looks odd because a 20x4 is wired like a 40x2 folded in half:
 * row 2 continues row 0 (0x00 + 20 = 0x14) and row 3 continues row 1. */
static const uint8_t s_row_addr[LCD_ROWS] = { 0x00, 0x40, 0x14, 0x54 };

static i2c_master_dev_handle_t s_dev;

/* Send 4 bits to the LCD.
 * @param nibble  value 0..15, placed on D4..D7 (PCF8574 P4..P7)
 * @param rs      0 = command, non-zero = character data
 * The backlight bit is set in every write, otherwise the backlight would
 * switch off. I2C errors are ignored (a missing LCD just shows nothing). */
static void write_nibble(uint8_t nibble, uint8_t rs)
{
    uint8_t pins = (uint8_t)(nibble << 4) | PCF_BL | (rs ? PCF_RS : 0);
    uint8_t buf[2] = { pins | PCF_EN, pins };    /* EN high then low latches the nibble */
    i2c_master_transmit(s_dev, buf, sizeof(buf), 100);
    /* Most HD44780 commands need ~40 us to execute before the next one */
    esp_rom_delay_us(50);
}

/* Send a full byte as two nibbles, high half first (4-bit mode order) */
static void write_byte(uint8_t b, uint8_t rs)
{
    write_nibble(b >> 4, rs);
    write_nibble(b & 0x0F, rs);
}

/* Send an HD44780 command byte (RS = 0) */
static void command(uint8_t c) { write_byte(c, 0); }

esp_err_t lcd_init(void)
{
    esp_err_t err = i2c_bus_add_device(I2C_ADDR_LCD, &s_dev);
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(50));               /* power-on settle time */

    /* HD44780 "initialisation by instruction" into 4-bit mode */
    /* After power-up the LCD may be in 8-bit mode or half-way through a
     * 4-bit byte; we cannot know. Sending "8-bit mode" (0x3) three times
     * with the datasheet's waits (>4.1 ms, >100 us) forces a known 8-bit
     * state, then 0x2 switches to 4-bit. From here on full bytes are sent
     * as two nibbles. */
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
    /* Clear takes ~1.5 ms inside the LCD, much longer than other commands */
    vTaskDelay(pdMS_TO_TICKS(2));
}

void lcd_print_row(uint8_t row, const char *text)
{
    if (row >= LCD_ROWS) {
        return;
    }
    /* Move the cursor to column 0 of this row, then write 20 characters;
     * the LCD advances the cursor itself (CMD_ENTRY_INC). */
    command(CMD_SET_DDRAM | s_row_addr[row]);
    for (int col = 0; col < LCD_COLS; col++) {
        /* Copy text until its NUL, then fill the rest of the row with
         * spaces so old characters are overwritten. */
        char c = (text && *text) ? *text++ : ' ';
        write_byte((uint8_t)c, 1);
    }
}
