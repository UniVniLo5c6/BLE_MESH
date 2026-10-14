#include "sensor.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_adc/adc_oneshot.h"
#include "dht.h"
#include "driver/gpio.h"

#define MQ2_GPIO ADC_CHANNEL_6 /// GPIO34 on esp32 
#define DHT_GPIO GPIO_NUM_25 ///
#define TAG "SENSOR"

static adc_oneshot_unit_handle_t adc1_handle; ///
static float last_temp = 0;

int read_mq2_value(void)
{
    int adc_raw = 0;

    adc_oneshot_read(adc1_handle, MQ2_GPIO, &adc_raw);

    return adc_raw;
}

bool read_dht(float *temp, float *humi)
{
    esp_err_t err;

    err = dht_read_float_data(
            DHT_TYPE_DHT11,//DHT11,AM2302
            DHT_GPIO,
            humi,
            temp);

    if (err != ESP_OK) {

        ESP_LOGE(TAG, "DHT error");

        return false;
    }

    return true;
}

void sensor_init(void)
{
    adc_oneshot_unit_init_cfg_t adc1_config = {
        .unit_id = ADC_UNIT_1,
    };

    adc_oneshot_new_unit(&adc1_config, &adc1_handle);

    adc_oneshot_chan_cfg_t mq2_channel_config = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };

    adc_oneshot_config_channel(
            adc1_handle,
            MQ2_GPIO,
            &mq2_channel_config);

    ESP_LOGI(TAG, "Sensor initialized");
}

bool sensor_read(sensor_data_t *data)
{
    float temp;
    float humi;

    data->mq2_adc = read_mq2_value();

    if (!read_dht(&temp, &humi)) {
        return false;
    }

    data->temp_x10 = temp * 10;
    data->humi_x10 = humi * 10;

    float delta_temp = temp - last_temp;

    int mq2 = data->mq2_adc;

    data->fire_level = 0;

        // 1. WARNING: có khói nhẹ
        if (mq2 > 1500) {
            data->fire_level = 1;
        }

        // 2. HIGH RISK: khói cao hoặc nhiệt cao
        if (mq2 > 2500 || temp > 36) {
            data->fire_level = 2;
        }

        // 3. FIRE CONFIRMED: kết hợp nhiều điều kiện
        if (mq2 > 3000 && (temp > 40 || delta_temp > 1.5)) {
            data->fire_level = 3;
        }

    last_temp = temp;

    ESP_LOGI(TAG,
        "MQ2=%d TEMP=%.1f HUMI=%.1f FIRE=%d",
        data->mq2_adc,
        data->temp_x10 / 10.0,
        data->humi_x10 / 10.0,
        data->fire_level);

    return true;
}