#include "app_web.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "app_activity_led.h"
#include "app_sample.h"
#include "app_wifi.h"
#include "esp_app_desc.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

extern const unsigned char webui_index_html_gz_start[] asm("_binary_webui_index_html_gz_start");
extern const unsigned char webui_index_html_gz_end[] asm("_binary_webui_index_html_gz_end");
extern const unsigned char webui_assets_index_js_gz_start[] asm("_binary_webui_assets_index_js_gz_start");
extern const unsigned char webui_assets_index_js_gz_end[] asm("_binary_webui_assets_index_js_gz_end");
extern const unsigned char webui_assets_index_css_gz_start[] asm("_binary_webui_assets_index_css_gz_start");
extern const unsigned char webui_assets_index_css_gz_end[] asm("_binary_webui_assets_index_css_gz_end");

#define APP_POST_BODY_MAX_LEN 512
#define APP_OTA_BUFFER_LEN 1024

static const char *TAG = "app_web";

static esp_err_t send_gzip_asset(httpd_req_t *req, const char *type, const unsigned char *start, const unsigned char *end)
{
    httpd_resp_set_type(req, type);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    return httpd_resp_send(req, (const char *)start, end - start);
}

static esp_err_t index_get_handler(httpd_req_t *req)
{
    app_activity_led_pulse();
    return send_gzip_asset(req, "text/html", webui_index_html_gz_start, webui_index_html_gz_end);
}

static esp_err_t script_get_handler(httpd_req_t *req)
{
    app_activity_led_pulse();
    return send_gzip_asset(req, "application/javascript", webui_assets_index_js_gz_start, webui_assets_index_js_gz_end);
}

static esp_err_t style_get_handler(httpd_req_t *req)
{
    app_activity_led_pulse();
    return send_gzip_asset(req, "text/css", webui_assets_index_css_gz_start, webui_assets_index_css_gz_end);
}

static const char *bool_text(bool value)
{
    return value ? "true" : "false";
}

static const char *partition_type_text(esp_partition_type_t type)
{
    switch (type) {
    case ESP_PARTITION_TYPE_APP:
        return "app";
    case ESP_PARTITION_TYPE_DATA:
        return "data";
    case ESP_PARTITION_TYPE_BOOTLOADER:
        return "bootloader";
    case ESP_PARTITION_TYPE_PARTITION_TABLE:
        return "partition_table";
    default:
        return "unknown";
    }
}

static void partition_subtype_text(const esp_partition_t *partition, char *out, size_t out_len)
{
    if (partition->type == ESP_PARTITION_TYPE_APP) {
        if (partition->subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY) {
            strlcpy(out, "factory", out_len);
        } else if (partition->subtype >= ESP_PARTITION_SUBTYPE_APP_OTA_MIN &&
                   partition->subtype < ESP_PARTITION_SUBTYPE_APP_OTA_MAX) {
            snprintf(out, out_len, "ota_%u", (unsigned)(partition->subtype - ESP_PARTITION_SUBTYPE_APP_OTA_MIN));
        } else if (partition->subtype == ESP_PARTITION_SUBTYPE_APP_TEST) {
            strlcpy(out, "test", out_len);
        } else {
            snprintf(out, out_len, "0x%02x", (unsigned)partition->subtype);
        }
    } else if (partition->type == ESP_PARTITION_TYPE_DATA) {
        switch (partition->subtype) {
        case ESP_PARTITION_SUBTYPE_DATA_OTA:
            strlcpy(out, "ota", out_len);
            break;
        case ESP_PARTITION_SUBTYPE_DATA_PHY:
            strlcpy(out, "phy", out_len);
            break;
        case ESP_PARTITION_SUBTYPE_DATA_NVS:
            strlcpy(out, "nvs", out_len);
            break;
        case ESP_PARTITION_SUBTYPE_DATA_COREDUMP:
            strlcpy(out, "coredump", out_len);
            break;
        case ESP_PARTITION_SUBTYPE_DATA_NVS_KEYS:
            strlcpy(out, "nvs_keys", out_len);
            break;
        case ESP_PARTITION_SUBTYPE_DATA_FAT:
            strlcpy(out, "fat", out_len);
            break;
        case ESP_PARTITION_SUBTYPE_DATA_SPIFFS:
            strlcpy(out, "spiffs", out_len);
            break;
        case ESP_PARTITION_SUBTYPE_DATA_LITTLEFS:
            strlcpy(out, "littlefs", out_len);
            break;
        default:
            snprintf(out, out_len, "0x%02x", (unsigned)partition->subtype);
            break;
        }
    } else {
        snprintf(out, out_len, "0x%02x", (unsigned)partition->subtype);
    }
}

static esp_err_t info_get_handler(httpd_req_t *req)
{
    app_activity_led_pulse();

    app_wifi_status_t wifi;
    app_wifi_get_status(&wifi);

    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_app_desc_t *app = esp_app_get_description();

    char json[768];
    int len = snprintf(json, sizeof(json),
                       "{"
                       "\"appName\":\"iot_hx711\","
                       "\"appVersion\":\"%s\","
                       "\"idfVersion\":\"%s\","
                       "\"target\":\"%s\","
                       "\"freeHeap\":%" PRIu32 ","
                       "\"minFreeHeap\":%" PRIu32 ","
                       "\"partition\":\"%s\","
                       "\"wifi\":{"
                       "\"hasCredentials\":%s,"
                       "\"connected\":%s,"
                       "\"softapActive\":%s,"
                       "\"ssid\":\"%s\","
                       "\"ip\":\"%s\","
                       "\"apSsid\":\"%s\""
                       "}"
                       "}",
                       app->version,
                       app->idf_ver,
                       CONFIG_IDF_TARGET,
                       esp_get_free_heap_size(),
                       esp_get_minimum_free_heap_size(),
                       running ? running->label : "unknown",
                       bool_text(wifi.has_credentials),
                       bool_text(wifi.connected),
                       bool_text(wifi.softap_active),
                       wifi.ssid,
                       wifi.ip,
                       wifi.ap_ssid);

    if (len < 0 || len >= (int)sizeof(json)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Info response too large");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, json);
}

static esp_err_t wifi_get_handler(httpd_req_t *req)
{
    app_activity_led_pulse();

    app_wifi_status_t wifi;
    app_wifi_get_status(&wifi);

    char json[384];
    int len = snprintf(json, sizeof(json),
                       "{"
                       "\"hasCredentials\":%s,"
                       "\"connected\":%s,"
                       "\"softapActive\":%s,"
                       "\"ssid\":\"%s\","
                       "\"ip\":\"%s\","
                       "\"apSsid\":\"%s\""
                       "}",
                       bool_text(wifi.has_credentials),
                       bool_text(wifi.connected),
                       bool_text(wifi.softap_active),
                       wifi.ssid,
                       wifi.ip,
                       wifi.ap_ssid);

    if (len < 0 || len >= (int)sizeof(json)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "WiFi response too large");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, json);
}

static esp_err_t sample_get_handler(httpd_req_t *req)
{
    app_activity_led_pulse();

    char json[256];
    esp_err_t err = app_sample_get_json(json, sizeof(json));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read cached sample: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Sample unavailable");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, json);
}

static esp_err_t partitions_get_handler(httpd_req_t *req)
{
    app_activity_led_pulse();

    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *boot = esp_ota_get_boot_partition();
    const esp_partition_t *next_update = esp_ota_get_next_update_partition(NULL);
    esp_partition_iterator_t iterator = esp_partition_find(
        ESP_PARTITION_TYPE_ANY,
        ESP_PARTITION_SUBTYPE_ANY,
        NULL);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t err = httpd_resp_sendstr_chunk(req, "{\"partitions\":[");
    if (err != ESP_OK) {
        return err;
    }

    bool first = true;
    while (iterator != NULL) {
        const esp_partition_t *partition = esp_partition_get(iterator);
        char subtype[24];
        partition_subtype_text(partition, subtype, sizeof(subtype));

        char json[320];
        int len = snprintf(json, sizeof(json),
                           "%s{"
                           "\"label\":\"%s\","
                           "\"type\":\"%s\","
                           "\"subtype\":\"%s\","
                           "\"address\":%" PRIu32 ","
                           "\"size\":%" PRIu32 ","
                           "\"encrypted\":%s,"
                           "\"running\":%s,"
                           "\"boot\":%s,"
                           "\"nextUpdate\":%s"
                           "}",
                           first ? "" : ",",
                           partition->label,
                           partition_type_text(partition->type),
                           subtype,
                           partition->address,
                           partition->size,
                           bool_text(partition->encrypted),
                           bool_text(partition == running),
                           bool_text(partition == boot),
                           bool_text(partition == next_update));

        if (len < 0 || len >= (int)sizeof(json)) {
            esp_partition_iterator_release(iterator);
            httpd_resp_sendstr_chunk(req, NULL);
            return ESP_FAIL;
        }

        err = httpd_resp_sendstr_chunk(req, json);
        if (err != ESP_OK) {
            esp_partition_iterator_release(iterator);
            return err;
        }
        first = false;
        iterator = esp_partition_next(iterator);
    }
    esp_partition_iterator_release(iterator);

    ESP_RETURN_ON_ERROR(httpd_resp_sendstr_chunk(req, "]}"), TAG, "Failed to finish partition response");
    return httpd_resp_sendstr_chunk(req, NULL);
}

static bool extract_json_string(const char *json, const char *key, char *out, size_t out_len)
{
    char pattern[32];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);

    const char *cursor = strstr(json, pattern);
    if (cursor == NULL) {
        return false;
    }

    cursor = strchr(cursor + strlen(pattern), ':');
    if (cursor == NULL) {
        return false;
    }

    cursor++;
    while (*cursor == ' ' || *cursor == '\t') {
        cursor++;
    }
    if (*cursor != '"') {
        return false;
    }
    cursor++;

    size_t used = 0;
    while (*cursor != '\0' && *cursor != '"' && used + 1 < out_len) {
        if (*cursor == '\\' && cursor[1] != '\0') {
            cursor++;
        }
        out[used++] = *cursor++;
    }
    out[used] = '\0';

    return *cursor == '"';
}

static void restart_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(750));
    esp_restart();
}

static esp_err_t wifi_post_handler(httpd_req_t *req)
{
    app_activity_led_pulse();

    if (req->content_len <= 0 || req->content_len >= APP_POST_BODY_MAX_LEN) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request body");
        return ESP_FAIL;
    }

    char body[APP_POST_BODY_MAX_LEN] = {0};
    int received = 0;
    while (received < req->content_len) {
        int ret = httpd_req_recv(req, body + received, req->content_len - received);
        if (ret <= 0) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read body");
            return ESP_FAIL;
        }
        received += ret;
        app_activity_led_pulse();
    }

    char ssid[APP_WIFI_SSID_MAX_LEN + 1] = {0};
    char password[APP_WIFI_PASSWORD_MAX_LEN + 1] = {0};
    if (!extract_json_string(body, "ssid", ssid, sizeof(ssid)) ||
        !extract_json_string(body, "password", password, sizeof(password)) ||
        ssid[0] == '\0') {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Expected ssid and password");
        return ESP_FAIL;
    }

    esp_err_t err = app_wifi_save_credentials(ssid, password);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to store WiFi credentials: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to store credentials");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true,\"restarting\":true}");
    xTaskCreate(restart_task, "restart_task", 2048, NULL, 5, NULL);
    return ESP_OK;
}

static esp_err_t wifi_forget_post_handler(httpd_req_t *req)
{
    app_activity_led_pulse();

    esp_err_t err = app_wifi_forget_credentials();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to forget WiFi credentials: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to forget credentials");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true,\"restarting\":true}");
    xTaskCreate(restart_task, "restart_task", 2048, NULL, 5, NULL);
    return ESP_OK;
}

static esp_err_t update_post_handler(httpd_req_t *req)
{
    app_activity_led_pulse();

    if (req->content_len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Firmware body is empty");
        return ESP_FAIL;
    }

    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    if (update_partition == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No OTA partition available");
        return ESP_FAIL;
    }

    esp_ota_handle_t ota_handle = 0;
    esp_err_t err = esp_ota_begin(update_partition, req->content_len, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA begin failed");
        return ESP_FAIL;
    }

    char buffer[APP_OTA_BUFFER_LEN];
    int remaining = req->content_len;
    while (remaining > 0) {
        int read_len = remaining < APP_OTA_BUFFER_LEN ? remaining : APP_OTA_BUFFER_LEN;
        int received = httpd_req_recv(req, buffer, read_len);
        if (received <= 0) {
            esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA upload failed");
            return ESP_FAIL;
        }

        err = esp_ota_write(ota_handle, buffer, received);
        if (err != ESP_OK) {
            esp_ota_abort(ota_handle);
            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA write failed");
            return ESP_FAIL;
        }

        remaining -= received;
        app_activity_led_pulse();
    }

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA image invalid");
        return ESP_FAIL;
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA boot partition failed");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true,\"restarting\":true}");
    xTaskCreate(restart_task, "restart_task", 2048, NULL, 5, NULL);
    return ESP_OK;
}

esp_err_t app_web_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 12;
    config.uri_match_fn = httpd_uri_match_wildcard;

    httpd_handle_t server = NULL;
    esp_err_t err = httpd_start(&server, &config);
    if (err != ESP_OK) {
        return err;
    }

    const httpd_uri_t routes[] = {
        {.uri = "/", .method = HTTP_GET, .handler = index_get_handler},
        {.uri = "/assets/index.js", .method = HTTP_GET, .handler = script_get_handler},
        {.uri = "/assets/index.css", .method = HTTP_GET, .handler = style_get_handler},
        {.uri = "/api/info", .method = HTTP_GET, .handler = info_get_handler},
        {.uri = "/api/wifi", .method = HTTP_GET, .handler = wifi_get_handler},
        {.uri = "/api/wifi", .method = HTTP_POST, .handler = wifi_post_handler},
        {.uri = "/api/wifi/forget", .method = HTTP_POST, .handler = wifi_forget_post_handler},
        {.uri = "/api/partitions", .method = HTTP_GET, .handler = partitions_get_handler},
        {.uri = "/sample", .method = HTTP_GET, .handler = sample_get_handler},
        {.uri = "/update", .method = HTTP_POST, .handler = update_post_handler},
    };

    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &routes[i]), TAG, "Failed to register route");
    }

    ESP_LOGI(TAG, "Web server started");
    return ESP_OK;
}
