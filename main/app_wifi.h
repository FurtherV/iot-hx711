#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_WIFI_SSID_MAX_LEN 32
#define APP_WIFI_PASSWORD_MAX_LEN 64
#define APP_WIFI_IP_MAX_LEN 16

typedef struct {
    bool has_credentials;
    bool connected;
    bool softap_active;
    char ssid[APP_WIFI_SSID_MAX_LEN + 1];
    char ip[APP_WIFI_IP_MAX_LEN];
    char ap_ssid[APP_WIFI_SSID_MAX_LEN + 1];
} app_wifi_status_t;

esp_err_t app_wifi_start(void);
esp_err_t app_wifi_save_credentials(const char *ssid, const char *password);
esp_err_t app_wifi_forget_credentials(void);
void app_wifi_get_status(app_wifi_status_t *status);

#ifdef __cplusplus
}
#endif
