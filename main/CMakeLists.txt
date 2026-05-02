idf_component_register(SRCS "main.c" "app_wifi.c" "app_web.c"
                       PRIV_REQUIRES
                            app_update
                            esp_app_format
                            esp_event
                            esp_http_server
                            esp_netif
                            esp_partition
                            esp_wifi
                            nvs_flash
                            spi_flash
                       INCLUDE_DIRS "")
