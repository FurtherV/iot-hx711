idf_component_register(SRCS "main.c" "app_wifi.c" "app_web.c" "app_mdns.c" "app_activity_led.c"
                       PRIV_REQUIRES
                            app_update
                            driver
                            esp_app_format
                            esp_event
                            esp_http_server
                            esp_netif
                            esp_partition
                            esp_wifi
                            mdns
                            nvs_flash
                            spi_flash
                       INCLUDE_DIRS "")
