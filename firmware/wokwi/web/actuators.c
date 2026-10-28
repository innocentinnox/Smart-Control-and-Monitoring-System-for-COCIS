/* Servo, lamp and buzzer drivers on top of the pwm.c LEDC helpers.
 *
 * Each function only converts "human" units (degrees, percent, Hz) into the
 * raw LEDC duty/frequency values; pwm.c does the actual register writes.
 */
#include "actuators.h"

#include "board_pins.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pwm.h"

/* SG90 servo timing: one pulse every 20 ms (50 Hz); the pulse width sets the
 * angle, 0.5 ms = 0 degrees ... 2.5 ms = 180 degrees. */
#define SERVO_FREQ_HZ   50
#define SERVO_PERIOD_US 20000
#define SERVO_MIN_US    500
#define SERVO_MAX_US    2500
/* Lamp PWM frequency in Hz: high enough that dimming does not flicker */
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
    /* Angle -> pulse width: linear between SERVO_MIN_US and SERVO_MAX_US.
     * e.g. 90 degrees -> 500 + 90 * 2000 / 180 = 1500 us. */
    uint32_t pulse_us = SERVO_MIN_US + (uint32_t)degrees * (SERVO_MAX_US - SERVO_MIN_US) / 180;
    /* Pulse width -> duty: the pulse is pulse_us out of a 20000 us period,
     * so duty = pulse_us / 20000 of the full scale. Multiply first so the
     * integer division does not throw the precision away
     * (1500 us -> 1500 * 8191 / 20000 = 614 counts = 7.5 %). */
    pwm_set_duty(ch, pulse_us * PWM_MAX_DUTY / SERVO_PERIOD_US);
}

/* ---------------- Lamp ---------------- */
void lamp_setup(ledc_channel_t ch, int gpio)
{
    pwm_timer_setup(PWM_TIMER_LIGHT, LAMP_FREQ_HZ);
    pwm_channel_setup(ch, PWM_TIMER_LIGHT, gpio);
}

void lamp_set(ledc_channel_t ch, uint8_t percent)
{
    if (percent > 100) percent = 100;
    /* Percent -> raw duty, e.g. 50 % -> 8191 * 50 / 100 = 4095 counts */
    pwm_set_duty(ch, PWM_MAX_DUTY * percent / 100);
}

/* ---------------- Buzzer ---------------- */
void buzzer_setup(int gpio)
{
    /* 2000 Hz is only a starting value: buzzer_tone() sets the real pitch
     * before every beep. The channel starts with duty 0 = silent. */
    pwm_timer_setup(PWM_TIMER_BUZZER, 2000);
    pwm_channel_setup(PWM_CH_BUZZER, PWM_TIMER_BUZZER, gpio);
}

void buzzer_tone(uint32_t freq_hz, uint32_t ms)
{
    pwm_set_freq(PWM_TIMER_BUZZER, freq_hz);
    pwm_set_duty(PWM_CH_BUZZER, PWM_MAX_DUTY / 2);      /* 50 % square wave */
    /* vTaskDelay sleeps this task for a number of FreeRTOS "ticks" (the
     * scheduler's time unit); pdMS_TO_TICKS converts ms to ticks. Other
     * tasks run meanwhile while the hardware keeps producing the tone. */
    vTaskDelay(pdMS_TO_TICKS(ms));
    pwm_set_duty(PWM_CH_BUZZER, 0);
}
