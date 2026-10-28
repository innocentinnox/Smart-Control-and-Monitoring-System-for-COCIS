/* 20x4 character LCD (HD44780) behind a PCF8574 I2C backpack.
 * Not thread-safe by design: only the ui task may call it.
 *
 * The HD44780 is the classic text-LCD controller. The PCF8574 "backpack" is
 * an 8-bit I2C port expander soldered to the back of the LCD at address 0x27
 * (I2C_ADDR_LCD); every byte we send over I2C sets its 8 output pins, which
 * are wired to the LCD's control lines and 4 of its data lines. That is why
 * the LCD is driven in 4-bit mode (see lcd2004.c).
 */
#pragma once

#include <stdint.h>
#include "esp_err.h"

/* Display size in characters */
#define LCD_COLS 20
#define LCD_ROWS 4

/* Attach the LCD to the shared I2C bus and run the HD44780 start-up
 * sequence (4-bit mode, display on, no cursor, cleared screen).
 * @return ESP_OK, or the I2C error if the device could not be attached.
 *         Note: errors while talking to the LCD itself are not detected, so
 *         ESP_OK does not prove an LCD is connected.
 * Blocks for roughly 60 ms. Call once, before any other lcd_* call.
 */
esp_err_t lcd_init(void);

/* Blank the whole screen and move the cursor to row 0, column 0.
 * Blocks for about 2 ms (the clear command is slow inside the LCD).
 */
void lcd_clear(void);

/* Write a whole row, padded with spaces / cut to exactly LCD_COLS.
 * @param row   0..LCD_ROWS-1 (0 = top); other values are ignored
 * @param text  NUL-terminated ASCII text; NULL is treated as an empty row
 * Takes about 20 x 2 I2C writes (a few ms at 100 kHz).
 */
void lcd_print_row(uint8_t row, const char *text);
