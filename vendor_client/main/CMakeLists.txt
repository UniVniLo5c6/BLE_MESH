set(srcs "main.c"
        "board.c")

idf_component_register(SRCS "${srcs}"
                    INCLUDE_DIRS  "."
                    REQUIRES esp_driver_gpio mqtt esp_wifi esp_event esp_netif nvs_flash
                    EMBED_TXTFILES "cert/ca.pem")
