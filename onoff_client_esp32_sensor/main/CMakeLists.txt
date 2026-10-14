idf_component_register(
    SRCS
        "main.c"
        "board.c"

    INCLUDE_DIRS
        "."
    REQUIRES bt
    PRIV_REQUIRES
        esp_adc dht nvs_flash
)