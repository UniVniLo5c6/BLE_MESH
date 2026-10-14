
#include "alarm.h"
#include "driver/gpio.h"
#include "esp_log.h"

#define TAG "ALARM"
#define ALARM_GPIO GPIO_NUM_22
#define LEDW_ALARM GPIO_NUM_21
#define LEDB_ALARM GPIO_NUM_19


void alarm_init(void)
{
 gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << ALARM_GPIO) | (1ULL << LEDW_ALARM) | (1ULL << LEDB_ALARM),
        .pull_down_en = 0,
        .pull_up_en = 0,
    };
    gpio_config(&io_conf);
    
    gpio_set_level(ALARM_GPIO, 0);
    gpio_set_level(LEDW_ALARM, 0);
    gpio_set_level(LEDB_ALARM, 0);

    ESP_LOGI(TAG, "Alarm initialized");
}

void alarm(uint8_t level)
{
    if (level == 0) {
        gpio_set_level(ALARM_GPIO, 0);
        gpio_set_level(LEDW_ALARM, 0);
        gpio_set_level(LEDB_ALARM, 0);
        ESP_LOGI(TAG, "Alarm OFF");
    } else {
        gpio_set_level(ALARM_GPIO, 1);
        gpio_set_level(LEDW_ALARM, 1);
        gpio_set_level(LEDB_ALARM, 1);
        ESP_LOGI(TAG, "Alarm ON");
    }
}