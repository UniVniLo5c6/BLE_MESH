#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef struct {

    uint16_t mq2_adc;

    int16_t temp_x10;

    int16_t humi_x10;

    uint8_t fire_level;

} sensor_data_t;

int read_mq2_value(void);
bool read_dht(float *temp, float *humi);
void sensor_init(void);
bool sensor_read(sensor_data_t *data);