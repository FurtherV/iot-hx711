#include "app_mdns.h"

#include <stdio.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "mdns.h"

#define APP_MDNS_HOSTNAME_MAX_LEN 16
#define APP_MDNS_HTTP_PORT 80

static const char *TAG = "app_mdns";

static void build_mdns_hostname(char *hostname, size_t hostname_len)
{
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    snprintf(hostname, hostname_len, "iot-%02X%02X%02X", mac[3], mac[4], mac[5]);
}

esp_err_t app_mdns_start(void)
{
    char hostname[APP_MDNS_HOSTNAME_MAX_LEN] = {0};
    build_mdns_hostname(hostname, sizeof(hostname));

    ESP_RETURN_ON_ERROR(mdns_init(), TAG, "Failed to initialize mDNS");
    ESP_RETURN_ON_ERROR(mdns_hostname_set(hostname), TAG, "Failed to set mDNS hostname");
    ESP_RETURN_ON_ERROR(mdns_instance_name_set("iot_hx711"), TAG, "Failed to set mDNS instance name");
    ESP_RETURN_ON_ERROR(mdns_service_add("iot_hx711", "_http", "_tcp", APP_MDNS_HTTP_PORT, NULL, 0),
                        TAG, "Failed to add mDNS HTTP service");

    ESP_LOGI(TAG, "mDNS started: http://%s.local", hostname);
    return ESP_OK;
}
