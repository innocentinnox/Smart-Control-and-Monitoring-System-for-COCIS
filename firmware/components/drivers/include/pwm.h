/* LEDC (PWM) helpers shared by servos, lamps and the buzzer.
 * All duty values here are in percent-of-period units or raw ticks of a
 * PWM_RES_BITS timer, so callers never deal with LEDC structs directly. */
#pragma once

#include <stdint.h>
#include "driver/ledc.h"

#define PWM_RES_BITS  LEDC_TIMER_13_BIT
#define PWM_MAX_DUTY  ((1u << 13) - 1)

void pwm_timer_setup(ledc_timer_t timer, uint32_t freq_hz);
void pwm_channel_setup(ledc_channel_t ch, ledc_timer_t timer, int gpio);

/* Safe from any task or timer callback (LEDC guards each write with a spinlock). */
void pwm_set_duty(ledc_channel_t ch, uint32_t duty);

void pwm_set_freq(ledc_timer_t timer, uint32_t freq_hz);
