/* Shared I2C master bus (LCD backpack 0x27 + DS1307 RTC 0x68).
 * Uses the ESP-IDF v5 "i2c_master" driver: one bus handle, plus one device
 * handle per chip that remembers its address and clock speed. */
#include "i2c_bus.h"

#include "board_pins.h"

static i2c_master_bus_handle_t s_bus;

esp_err_t i2c_bus_init(void)
{
    /* glitch_ignore_cnt filters out spikes shorter than 7 clock cycles on
     * SDA/SCL. The internal pull-ups are weak, but the bus lines need
     * pull-ups to work at all, and in Wokwi they are enough. */
    i2c_master_bus_config_t cfg = {
        .i2c_port          = I2C_NUM_0,
        .sda_io_num        = PIN_I2C_SDA,
        .scl_io_num        = PIN_I2C_SCL,
        .clk_source        = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    return i2c_new_master_bus(&cfg, &s_bus);
}

esp_err_t i2c_bus_add_device(uint8_t addr, i2c_master_dev_handle_t *out)
{
    i2c_device_config_t dev = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = addr,
        .scl_speed_hz    = I2C_FREQ_HZ,
    };
    return i2c_master_bus_add_device(s_bus, &dev, out);
}
