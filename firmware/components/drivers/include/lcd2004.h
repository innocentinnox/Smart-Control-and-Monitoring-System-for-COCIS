/* 20x4 character LCD (HD44780) behind a PCF8574 I2C backpack.
 * Not thread-safe by design: only the ui task may call it. */
#pragma once

#include <stdint.h>
#include "esp_err.h"

#define LCD_COLS 20
#define LCD_ROWS 4

esp_err_t lcd_init(void);
void lcd_clear(void);

/* Write a whole row, padded with spaces / cut to exactly LCD_COLS. */
void lcd_print_row(uint8_t row, const char *text);
