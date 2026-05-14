#include "app_wifi.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "nvs.h"
#include "sdkconfig.h"

#define APP_WIFI_NAMESPACE "wifi_config"
#define APP_WIFI_SSID_KEY "ssid"
#define APP_WIFI_PASSWORD_KEY "password"
#define APP_WIFI_AP_PASSWORD CONFIG_APP_WIFI_AP_PASSWORD
#define APP_WIFI_MAX_RETRIES 10
#define APP_WIFI_SCAN_RECORD_MAX 32

static const char *TAG = "app_wifi";
static const int WIFI_CONNECTED_BIT = BIT0;
static const int WIFI_FAIL_BIT = BIT1;

static EventGroupHandle_t s_wifi_event_group;
static int s_retry_count;
static app_wifi_status_t s_status;
static bool s_sta_connect_enabled;

static esp_err_t load_credentials(char *ssid, size_t ssid_len, char *password, size_t password_len)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(APP_WIFI_NAMESPACE, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_get_str(nvs, APP_WIFI_SSID_KEY, ssid, &ssid_len);
    if (err == ESP_OK) {
        err = nvs_get_str(nvs, APP_WIFI_PASSWORD_KEY, password, &password_len);
    }

    nvs_close(nvs);
    return err;
}

esp_err_t app_wifi_save_credentials(const char *ssid, const char *password)
{
    if (ssid == NULL || password == NULL || ssid[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    if (strlen(ssid) > APP_WIFI_SSID_MAX_LEN || strlen(password) > APP_WIFI_PASSWORD_MAX_LEN) {
        return ESP_ERR_INVALID_SIZE;
    }

    nvs_handle_t nvs;
    esp_err_t err = nvs_open(APP_WIFI_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_str(nvs, APP_WIFI_SSID_KEY, ssid);
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, APP_WIFI_PASSWORD_KEY, password);
    }
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }

    nvs_close(nvs);
    return err;
}

esp_err_t app_wifi_forget_credentials(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(APP_WIFI_NAMESPACE, NVS_READWRITE, &nvs);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_erase_all(nvs);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }

    nvs_close(nvs);
    return err;
}

esp_err_t app_wifi_scan_ssids(char ssids[][APP_WIFI_SSID_MAX_LEN + 1], size_t max_ssids, size_t *ssid_count)
{
    if (ssids == NULL || ssid_count == NULL || max_ssids == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    *ssid_count = 0;

    wifi_scan_config_t scan_config = {
        .show_hidden = false,
    };
    esp_err_t err = esp_wifi_scan_start(&scan_config, true);
    if (err != ESP_OK) {
        return err;
    }

    uint16_t ap_count = 0;
    err = esp_wifi_scan_get_ap_num(&ap_count);
    if (err != ESP_OK || ap_count == 0) {
        return err;
    }

    uint16_t record_count = ap_count > APP_WIFI_SCAN_RECORD_MAX ? APP_WIFI_SCAN_RECORD_MAX : ap_count;
    wifi_ap_record_t *records = calloc(record_count, sizeof(wifi_ap_record_t));
    if (records == NULL) {
        return ESP_ERR_NO_MEM;
    }

    err = esp_wifi_scan_get_ap_records(&record_count, records);
    if (err != ESP_OK) {
        free(records);
        return err;
    }

    int8_t rssi[APP_WIFI_SCAN_RESULT_MAX] = {0};
    size_t limit = max_ssids < APP_WIFI_SCAN_RESULT_MAX ? max_ssids : APP_WIFI_SCAN_RESULT_MAX;

    for (uint16_t i = 0; i < record_count; i++) {
        const char *ssid = (const char *)records[i].ssid;
        if (ssid[0] == '\0') {
            continue;
        }

        size_t existing = limit;
        for (size_t j = 0; j < *ssid_count; j++) {
            if (strncmp(ssids[j], ssid, APP_WIFI_SSID_MAX_LEN) == 0) {
                existing = j;
                break;
            }
        }

        if (existing < *ssid_count) {
            if (records[i].rssi > rssi[existing]) {
                rssi[existing] = records[i].rssi;
            }
            continue;
        }

        if (*ssid_count >= limit) {
            continue;
        }

        strlcpy(ssids[*ssid_count], ssid, APP_WIFI_SSID_MAX_LEN + 1);
        rssi[*ssid_count] = records[i].rssi;
        (*ssid_count)++;
    }

    for (size_t i = 0; i < *ssid_count; i++) {
        for (size_t j = i + 1; j < *ssid_count; j++) {
            if (rssi[j] > rssi[i]) {
                int8_t rssi_tmp = rssi[i];
                rssi[i] = rssi[j];
                rssi[j] = rssi_tmp;

                char ssid_tmp[APP_WIFI_SSID_MAX_LEN + 1];
                strlcpy(ssid_tmp, ssids[i], sizeof(ssid_tmp));
                strlcpy(ssids[i], ssids[j], APP_WIFI_SSID_MAX_LEN + 1);
                strlcpy(ssids[j], ssid_tmp, APP_WIFI_SSID_MAX_LEN + 1);
            }
        }
    }

    free(records);
    return ESP_OK;
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        if (s_sta_connect_enabled) {
            esp_wifi_connect();
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_status.connected = false;
        s_status.ip[0] = '\0';

        if (s_sta_connect_enabled && s_retry_count < APP_WIFI_MAX_RETRIES) {
            s_retry_count++;
            esp_wifi_connect();
            ESP_LOGI(TAG, "Retrying WiFi connection (%d/%d)", s_retry_count, APP_WIFI_MAX_RETRIES);
        } else if (s_sta_connect_enabled) {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        snprintf(s_status.ip, sizeof(s_status.ip), IPSTR, IP2STR(&event->ip_info.ip));
        s_status.connected = true;
        s_retry_count = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        ESP_LOGI(TAG, "Connected with IP %s", s_status.ip);
    }
}

static void build_ap_ssid(char *ssid, size_t ssid_len)
{
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    snprintf(ssid, ssid_len, "iot-%02X%02X%02X", mac[3], mac[4], mac[5]);
}

static esp_err_t start_softap(void)
{
    s_sta_connect_enabled = false;
    build_ap_ssid(s_status.ap_ssid, sizeof(s_status.ap_ssid));

    size_t ap_password_len = strlen(APP_WIFI_AP_PASSWORD);
    if (ap_password_len > 0 &&
        (ap_password_len < 8 || ap_password_len > APP_WIFI_PASSWORD_MAX_LEN - 1)) {
        ESP_LOGE(TAG, "Invalid SoftAP password length: %u", (unsigned)ap_password_len);
        return ESP_ERR_INVALID_SIZE;
    }

    wifi_config_t ap_config = {0};
    strlcpy((char *)ap_config.ap.ssid, s_status.ap_ssid, sizeof(ap_config.ap.ssid));
    strlcpy((char *)ap_config.ap.password, APP_WIFI_AP_PASSWORD, sizeof(ap_config.ap.password));
    ap_config.ap.ssid_len = strlen(s_status.ap_ssid);
    ap_config.ap.channel = 1;
    ap_config.ap.max_connection = 4;
    ap_config.ap.authmode = ap_password_len > 0 ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    ap_config.ap.pmf_cfg.required = false;

    esp_err_t stop_err = esp_wifi_stop();
    if (stop_err != ESP_OK && stop_err != ESP_ERR_WIFI_NOT_STARTED) {
        ESP_LOGE(TAG, "Failed to stop WiFi before SoftAP start: %s", esp_err_to_name(stop_err));
        return stop_err;
    }
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_APSTA), TAG, "Failed to set SoftAP mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &ap_config), TAG, "Failed to configure SoftAP");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "Failed to start SoftAP");

    s_status.softap_active = true;
    s_status.connected = false;
    strlcpy(s_status.ip, "192.168.4.1", sizeof(s_status.ip));
    ESP_LOGI(TAG, "%s SoftAP started: SSID=%s",
             ap_password_len > 0 ? "Password-protected" : "Open",
             s_status.ap_ssid);
    return ESP_OK;
}

esp_err_t app_wifi_start(void)
{
    char ssid[APP_WIFI_SSID_MAX_LEN + 1] = {0};
    char password[APP_WIFI_PASSWORD_MAX_LEN + 1] = {0};

    memset(&s_status, 0, sizeof(s_status));
    s_sta_connect_enabled = false;

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "Failed to initialize netif");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "Failed to create event loop");
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "Failed to initialize WiFi");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL),
                        TAG, "Failed to register WiFi event handler");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL),
                        TAG, "Failed to register IP event handler");

    s_wifi_event_group = xEventGroupCreate();
    if (s_wifi_event_group == NULL) {
        return ESP_ERR_NO_MEM;
    }

    if (load_credentials(ssid, sizeof(ssid), password, sizeof(password)) != ESP_OK) {
        ESP_LOGI(TAG, "No stored WiFi credentials, starting provisioning SoftAP");
        return start_softap();
    }

    s_status.has_credentials = true;
    strlcpy(s_status.ssid, ssid, sizeof(s_status.ssid));
    strlcpy(s_status.password, password, sizeof(s_status.password));

    wifi_config_t sta_config = {0};
    strlcpy((char *)sta_config.sta.ssid, ssid, sizeof(sta_config.sta.ssid));
    strlcpy((char *)sta_config.sta.password, password, sizeof(sta_config.sta.password));
    sta_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "Failed to set STA mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &sta_config), TAG, "Failed to configure STA");
    s_sta_connect_enabled = true;
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "Failed to start STA");

    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE,
        pdFALSE,
        pdMS_TO_TICKS(20000));

    if ((bits & WIFI_CONNECTED_BIT) == 0) {
        ESP_LOGW(TAG, "Stored WiFi credentials did not connect, starting provisioning SoftAP");
        return start_softap();
    }

    return ESP_OK;
}

void app_wifi_get_status(app_wifi_status_t *status)
{
    if (status == NULL) {
        return;
    }

    *status = s_status;
}
