/* Converts the Wokwi photoresistor module's AO voltage back to lux, using
 * the formula from the Wokwi part documentation (ratio = V / Vcc):
 *   R_ldr = 10k * ratio / (1 - ratio);  lux = (RL10 * 1e3 * 10^GAMMA / R_ldr)^(1/GAMMA)
 * Higher AO voltage = higher LDR resistance = darker room.
 *
 * Step by step:
 *   1. the ADC gives a raw number 0..4095 (12 bits) for 0..Vcc on the pin
 *   2. raw / 4095 = ratio = how far up the voltage divider AO sits
 *   3. the module is a voltage divider: a fixed 10 kOhm resistor and the
 *      LDR, so the ratio gives the LDR's resistance (formula above)
 *   4. an LDR's resistance falls as light rises, following a power law set
 *      by GAMMA and RL10 (its resistance at 10 lux); inverting that law
 *      gives lux
 *
 * ESP-IDF "oneshot" ADC mode = take one conversion on request (as opposed to
 * continuous/DMA sampling). The ADC unit can only do one conversion at a
 * time, and both room tasks read it, so a mutex guards each read.
 * A mutex is a lock: the task that takes it runs; any other task trying to
 * take it sleeps until it is given back.
 */
#include "light_sensor.h"

#include <math.h>
#include "esp_adc/adc_oneshot.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/* Maximum number of light_sensor_add() calls */
#define MAX_SENSORS 4
/* LDR curve steepness (Wokwi default for its photoresistor part) */
#define GAMMA       0.7
#define RL10_KOHM   50.0                /* LDR resistance at 10 lux */
/* Fixed resistor in the module's voltage divider, in ohms */
#define SERIES_OHM  10000.0
/* Largest 12-bit ADC reading (= Vcc on the pin) */
#define ADC_FULL    4095.0

static adc_oneshot_unit_handle_t s_adc;
/* Serialises adc_oneshot_read() between the room tasks */
static SemaphoreHandle_t         s_lock;
/* ADC channel of each registered sensor, indexed by handle */
static adc_channel_t             s_channels[MAX_SENSORS];
static int                       s_count;

esp_err_t light_sensor_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    adc_oneshot_unit_init_cfg_t cfg = { .unit_id = ADC_UNIT_1 };
    return adc_oneshot_new_unit(&cfg, &s_adc);
}

int light_sensor_add(int gpio)
{
    /* Translate the GPIO number to its ADC unit + channel, and refuse pins
     * that are not on ADC1 (only one ADC1 unit handle exists). */
    adc_unit_t unit;
    adc_channel_t ch;
    if (s_count >= MAX_SENSORS || adc_oneshot_io_to_channel(gpio, &unit, &ch) != ESP_OK ||
        unit != ADC_UNIT_1) {
        return -1;
    }
    /* 12 dB attenuation lets the pin measure (roughly) the full 0..3.3 V
     * range instead of only ~1 V; 12-bit result = 0..4095. */
    adc_oneshot_chan_cfg_t cc = { .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_12 };
    if (adc_oneshot_config_channel(s_adc, ch, &cc) != ESP_OK) {
        return -1;
    }
    s_channels[s_count] = ch;
    return s_count++;
}

/* Convert a raw ADC reading (0..4095) to lux with the Wokwi formula (see the
 * top of this file). The two ends are special-cased because the formula
 * would divide by zero there. */
static double raw_to_lux(int raw)
{
    double ratio = raw / ADC_FULL;              /* V / Vcc */
    if (ratio <= 0.0) return 100000.0;          /* saturated bright */
    if (ratio >= 1.0) return 0.0;               /* fully dark */
    double r_ldr = SERIES_OHM * ratio / (1.0 - ratio);
    return pow(RL10_KOHM * 1e3 * pow(10, GAMMA) / r_ldr, 1.0 / GAMMA);
}

int light_sensor_read_lux(int handle)
{
    if (handle < 0 || handle >= s_count) {
        return -1;
    }
    int raw = 0;
    /* Hold the lock only for the conversion itself; the floating-point
     * maths below does not need the ADC. */
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = adc_oneshot_read(s_adc, s_channels[handle], &raw);
    xSemaphoreGive(s_lock);
    if (err != ESP_OK) {
        return -1;
    }
    double lux = raw_to_lux(raw);
    /* Cap so the result fits in 16 bits (the bright case is 100000) */
    return lux > 65535 ? 65535 : (int)lux;
}
