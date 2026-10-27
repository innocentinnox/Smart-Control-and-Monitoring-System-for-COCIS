#include "pwm.h"

#define MODE LEDC_LOW_SPEED_MODE        /* the only LEDC mode on the ESP32-S3 */

void pwm_init(void)
{
    /* Needed for the hardware fades and the thread-safe duty API */
    ESP_ERROR_CHECK(ledc_fade_func_install(0));
}

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

void pwm_set_duty(ledc_channel_t ch, uint32_t duty)
{
    ledc_set_duty_and_update(MODE, ch, duty, 0);
}

void pwm_fade_to(ledc_channel_t ch, uint32_t duty, uint32_t fade_ms)
{
    ledc_set_fade_time_and_start(MODE, ch, duty, fade_ms, LEDC_FADE_NO_WAIT);
}

void pwm_set_freq(ledc_timer_t timer, uint32_t freq_hz)
{
    ledc_set_freq(MODE, timer, freq_hz);
}
