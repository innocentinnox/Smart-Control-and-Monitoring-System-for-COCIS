#include "actuators.h"

#include "board_pins.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pwm.h"

#define SERVO_FREQ_HZ   50
#define SERVO_PERIOD_US 20000
#define SERVO_MIN_US    500
#define SERVO_MAX_US    2500
#define LAMP_FREQ_HZ    5000

/* ---------------- Servo ---------------- */
void servo_setup(ledc_channel_t ch, int gpio)
{
    pwm_timer_setup(PWM_TIMER_SERVO, SERVO_FREQ_HZ);    /* re-applying the same config is harmless */
    pwm_channel_setup(ch, PWM_TIMER_SERVO, gpio);
    servo_set_angle(ch, 0);
}

void servo_set_angle(ledc_channel_t ch, int degrees)
{
    if (degrees < 0)   degrees = 0;
    if (degrees > 180) degrees = 180;
    uint32_t pulse_us = SERVO_MIN_US + (uint32_t)degrees * (SERVO_MAX_US - SERVO_MIN_US) / 180;
    pwm_set_duty(ch, pulse_us * PWM_MAX_DUTY / SERVO_PERIOD_US);
}

/* ---------------- Lamp ---------------- */
void lamp_setup(ledc_channel_t ch, int gpio)
{
    pwm_timer_setup(PWM_TIMER_LIGHT, LAMP_FREQ_HZ);
    pwm_channel_setup(ch, PWM_TIMER_LIGHT, gpio);
}

void lamp_fade_to(ledc_channel_t ch, uint8_t percent, uint32_t fade_ms)
{
    if (percent > 100) percent = 100;
    pwm_fade_to(ch, PWM_MAX_DUTY * percent / 100, fade_ms);
}

/* ---------------- Buzzer ---------------- */
void buzzer_setup(int gpio)
{
    pwm_timer_setup(PWM_TIMER_BUZZER, 2000);
    pwm_channel_setup(PWM_CH_BUZZER, PWM_TIMER_BUZZER, gpio);
}

void buzzer_tone(uint32_t freq_hz, uint32_t ms)
{
    pwm_set_freq(PWM_TIMER_BUZZER, freq_hz);
    pwm_set_duty(PWM_CH_BUZZER, PWM_MAX_DUTY / 2);      /* 50 % square wave */
    vTaskDelay(pdMS_TO_TICKS(ms));
    pwm_set_duty(PWM_CH_BUZZER, 0);
}
