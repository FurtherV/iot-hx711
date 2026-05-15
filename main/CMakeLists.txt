idf_component_register(SRCS "main.c" "app_wifi.c" "app_web.c" "app_mdns.c" "app_mqtt.c" "app_activity_led.c" "app_sample.c"
                       PRIV_REQUIRES
                            app_update
                            driver
                            esp_app_format
                            esp_event
                            esp_http_server
                            esp_netif
                            esp_partition
                            esp_wifi
                            esp-idf-lib__hx711
                            mdns
                            mqtt
                            nvs_flash
                            spi_flash
                       INCLUDE_DIRS "")
