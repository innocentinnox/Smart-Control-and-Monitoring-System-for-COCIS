/* LEDC (PWM) helpers shared by servos, lamps and the buzzer.
 * All duty values here are in percent-of-period units or raw ticks of a
 * PWM_RES_BITS timer, so callers never deal with LEDC structs directly.
 *
 * PWM (pulse-width modulation) switches a pin on and off very fast. The
 * "frequency" is how many on/off cycles per second; the "duty" is how long
 * the pin is on in each cycle. On the ESP32-S3 the LEDC peripheral does this
 * in hardware:
 *   - a LEDC *timer* produces the frequency (several channels may share it)
 *   - a LEDC *channel* connects one timer to one GPIO with its own duty
 * Duty here is a raw count 0..PWM_MAX_DUTY, where PWM_MAX_DUTY = always on
 * (e.g. PWM_MAX_DUTY / 2 = 50 % on).
 */
#pragma once

#include <stdint.h>
#include "driver/ledc.h"

/* 13-bit duty resolution: one period is split into 8192 steps */
#define PWM_RES_BITS  LEDC_TIMER_13_BIT
/* Largest duty value for PWM_RES_BITS (8191) = output (almost) always on */
#define PWM_MAX_DUTY  ((1u << 13) - 1)

/* Configure (or reconfigure) a LEDC timer with PWM_RES_BITS resolution.
 * @param timer    LEDC timer to set up (PWM_TIMER_* in board_pins.h)
 * @param freq_hz  PWM frequency in Hz (50 for servos, 5000 for lamps, ...)
 * Aborts (ESP_ERROR_CHECK) if the frequency cannot be produced at 13 bits.
 * Call at boot.
 */
void pwm_timer_setup(ledc_timer_t timer, uint32_t freq_hz);

/* Route a LEDC channel to a GPIO, driven by the given timer, duty 0 (off).
 * @param ch     LEDC channel (PWM_CH_* in board_pins.h), one per GPIO
 * @param timer  timer previously set up with pwm_timer_setup()
 * @param gpio   output GPIO number
 * Aborts (ESP_ERROR_CHECK) on invalid arguments. Call at boot.
 */
void pwm_channel_setup(ledc_channel_t ch, ledc_timer_t timer, int gpio);

/* Change a channel's duty cycle; takes effect at the start of the next PWM
 * period.
 * @param ch    LEDC channel
 * @param duty  raw duty 0..PWM_MAX_DUTY (0 = off)
 */
/* Safe from any task or timer callback (LEDC guards each write with a spinlock). */
void pwm_set_duty(ledc_channel_t ch, uint32_t duty);

/* Change a timer's frequency (used by the buzzer to change pitch). Every
 * channel on that timer changes frequency together.
 * @param timer    LEDC timer
 * @param freq_hz  new frequency in Hz
 * Errors from IDF are ignored.
 */
void pwm_set_freq(ledc_timer_t timer, uint32_t freq_hz);
