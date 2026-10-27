# drivers: thin hardware layers, each owning one peripheral type.
idf_component_register(SRCS "i2c_bus.c" "lcd2004.c" "ds1307.c" "keypad.c" "gpio_isr.c"
                            "buttons.c" "pwm.c" "actuators.c" "light_sensor.c"
                       INCLUDE_DIRS "include"
                       REQUIRES common esp_driver_i2c esp_driver_gpio esp_driver_ledc esp_adc
                       PRIV_REQUIRES esp_rom)
