/* Small actuators built on pwm.c: door servos, dimmable lamps, buzzer. */
#pragma once

#include <stdint.h>
#include "driver/ledc.h"

/* ---- Servo (SG90-style: 50 Hz, 0.5..2.5 ms pulse = 0..180 degrees) ---- */
void servo_setup(ledc_channel_t ch, int gpio);          /* also sets the shared 50 Hz timer */
void servo_set_angle(ledc_channel_t ch, int degrees);

/* ---- Dimmable lamp (LED driven by PWM) ---- */
void lamp_setup(ledc_channel_t ch, int gpio);
void lamp_fade_to(ledc_channel_t ch, uint8_t percent, uint32_t fade_ms);

/* ---- Passive buzzer ---- */
void buzzer_setup(int gpio);
void buzzer_tone(uint32_t freq_hz, uint32_t ms);       /* blocks the caller for ms */
