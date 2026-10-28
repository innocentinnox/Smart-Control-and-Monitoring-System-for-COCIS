#include "pwm.h"

#define MODE LEDC_LOW_SPEED_MODE        /* the only LEDC mode on the ESP32-S3 */

void pwm_timer_setup(ledc_timer_t timer, uint32_t freq_hz)
{
    ledc_timer_config_t cfg = {
        .speed_mode      = MODE,
        .duty_resolution = PWM_RES_BITS,
        .timer_num       = timer,
        .freq_hz         = freq_hz,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&cfg));
}

void pwm_channel_setup(ledc_channel_t ch, ledc_timer_t timer, int gpio)
{
    ledc_channel_config_t cfg = {
        .gpio_num   = gpio,
        .speed_mode = MODE,
        .channel    = ch,
        .timer_sel  = timer,
        .duty       = 0,
        .hpoint     = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&cfg));
}

/* No LEDC fade service on purpose: its interrupt storms in Wokwi (and the
 * peripheral keeps it pending across soft resets), so lamps ramp in software. */
void pwm_set_duty(ledc_channel_t ch, uint32_t duty)
{
    ledc_set_duty(MODE, ch, duty);
    ledc_update_duty(MODE, ch);
}

void pwm_set_freq(ledc_timer_t timer, uint32_t freq_hz)
{
    ledc_set_freq(MODE, timer, freq_hz);
}
