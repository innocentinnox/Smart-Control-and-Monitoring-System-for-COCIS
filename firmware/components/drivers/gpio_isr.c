#include "gpio_isr.h"

#include "driver/gpio.h"

/* Called by every driver that needs pin interrupts, all from app_main, so a
 * plain flag is enough (asking IDF twice prints a scary-looking error). */
void gpio_isr_service_ensure(void)
{
    static bool installed;
    if (!installed) {
        ESP_ERROR_CHECK(gpio_install_isr_service(0));
        installed = true;
    }
}
