set(srcs "main.c"
    "board.c"   )

idf_component_register(SRCS "${srcs}"
                    INCLUDE_DIRS  "."
                    REQUIRES bt nvs_flash esp_driver_gpio)
