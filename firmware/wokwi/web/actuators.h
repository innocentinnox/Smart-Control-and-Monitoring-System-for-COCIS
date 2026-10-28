/* Small actuators built on pwm.c: door servos, dimmable lamps, buzzer.
 *
 * Hardware driven:
 *   - SG90 hobby servos that swing the entrance doors (one per door side)
 *   - LED lamps in the rooms, dimmed by changing the PWM duty cycle
 *   - one passive buzzer at the entrance (a passive buzzer has no built-in
 *     oscillator: it only beeps when we feed it a square wave, and the
 *     square wave's frequency is the pitch you hear)
 *
 * All three use the ESP32-S3 LEDC (LED Control) PWM peripheral through the
 * thin pwm.c wrapper. LEDC has "timers" (which set the frequency) and
 * "channels" (which set the duty cycle on one GPIO and follow one timer).
 * Each actuator kind gets its own timer because each needs a different
 * frequency (see PWM_TIMER_* in board_pins.h).
 */
#pragma once

#include <stdint.h>
#include "driver/ledc.h"

/* ---- Servo (SG90-style: 50 Hz, 0.5..2.5 ms pulse = 0..180 degrees) ---- */

/* Attach a servo to a GPIO and move it to 0 degrees (door closed).
 * Also (re)configures the shared 50 Hz servo timer, so it is fine to call
 * this once per servo. Call once at boot.
 * @param ch    LEDC channel reserved for this servo (PWM_CH_DOOR_IN / _OUT)
 * @param gpio  GPIO number wired to the servo's signal (orange) wire
 */
void servo_setup(ledc_channel_t ch, int gpio);          /* also sets the shared 50 Hz timer */

/* Move a servo to an angle. Returns at once; the servo itself needs a few
 * hundred ms to physically reach the position.
 * @param ch       LEDC channel given to servo_setup()
 * @param degrees  target angle 0..180; values outside are clamped
 */
void servo_set_angle(ledc_channel_t ch, int degrees);

/* ---- Dimmable lamp (LED driven by PWM) ---- */

/* Attach a lamp LED to a GPIO on the 5 kHz light timer. The lamp starts off.
 * 5 kHz is far above what the eye can see, so dimming does not flicker.
 * Call once at boot.
 * @param ch    LEDC channel reserved for this lamp (PWM_CH_ROOMx_LIGHT)
 * @param gpio  GPIO number wired to the LED
 */
void lamp_setup(ledc_channel_t ch, int gpio);

/* Set lamp brightness.
 * @param ch       LEDC channel given to lamp_setup()
 * @param percent  brightness 0..100 (0 = off); values above 100 are clamped
 * Safe from any task (see pwm_set_duty).
 */
void lamp_set(ledc_channel_t ch, uint8_t percent);    /* instant; callers ramp if they want a fade */

/* ---- Passive buzzer ---- */

/* Attach the passive buzzer to a GPIO (uses PWM_CH_BUZZER on the buzzer
 * timer). It stays silent until buzzer_tone() is called. Call once at boot.
 * @param gpio  GPIO number wired to the buzzer
 */
void buzzer_setup(int gpio);

/* Play one tone, then go silent again.
 * @param freq_hz  pitch in Hz (the project uses roughly 400..2000 Hz)
 * @param ms       tone length in milliseconds
 * Blocks the calling task for ms (it sleeps with vTaskDelay, so other tasks
 * keep running). Only the ui task calls this, so tones never overlap.
 */
void buzzer_tone(uint32_t freq_hz, uint32_t ms);       /* blocks the caller for ms */
