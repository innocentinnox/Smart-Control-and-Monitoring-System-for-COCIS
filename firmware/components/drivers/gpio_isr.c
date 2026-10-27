#include "gpio_isr.h"

#include "driver/gpio.h"

void gpio_isr_service_ensure(void)
{
    esp_err_t err = gpio_install_isr_service(0);
    if (err != ESP_ERR_INVALID_STATE) {             /* INVALID_STATE = already installed */
        ESP_ERROR_CHECK(err);
    }
}
