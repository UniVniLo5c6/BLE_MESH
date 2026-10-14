set(srcs "main.c"
        "board.c"
        "sensor.c"
        "alarm.c")

idf_component_register(SRCS "${srcs}"
                    INCLUDE_DIRS  "."
                    REQUIRES esp_adc nvs_flash esp_driver_gpio)
