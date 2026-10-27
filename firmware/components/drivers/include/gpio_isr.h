/* The GPIO ISR service is shared by several drivers; install it once. */
#pragma once

void gpio_isr_service_ensure(void);
