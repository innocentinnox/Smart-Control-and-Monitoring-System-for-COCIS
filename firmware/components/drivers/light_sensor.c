/* Converts the Wokwi photoresistor module's AO voltage back to lux, using
 * the formula from the Wokwi part documentation (ratio = V / Vcc):
 *   R_ldr = 10k * ratio / (1 - ratio);  lux = (RL10 * 1e3 * 10^GAMMA / R_ldr)^(1/GAMMA)
 * Higher AO voltage = higher LDR resistance = darker room.
 */
#include "light_sensor.h"

#include <math.h>
#include "esp_adc/adc_oneshot.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define MAX_SENSORS 4
#define GAMMA       0.7
#define RL10_KOHM   50.0                /* LDR resistance at 10 lux */
#define SERIES_OHM  10000.0
#define ADC_FULL    4095.0

static adc_oneshot_unit_handle_t s_adc;
static SemaphoreHandle_t         s_lock;
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
    adc_unit_t unit;
    adc_channel_t ch;
    if (s_count >= MAX_SENSORS || adc_oneshot_io_to_channel(gpio, &unit, &ch) != ESP_OK ||
        unit != ADC_UNIT_1) {
        return -1;
    }
    adc_oneshot_chan_cfg_t cc = { .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_12 };
    if (adc_oneshot_config_channel(s_adc, ch, &cc) != ESP_OK) {
        return -1;
    }
    s_channels[s_count] = ch;
    return s_count++;
}

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
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = adc_oneshot_read(s_adc, s_channels[handle], &raw);
    xSemaphoreGive(s_lock);
    if (err != ESP_OK) {
        return -1;
    }
    double lux = raw_to_lux(raw);
    return lux > 65535 ? 65535 : (int)lux;
}
