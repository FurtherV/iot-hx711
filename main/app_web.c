#include "app_web.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

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

extern const unsigned char webui_index_html_start[] asm("_binary_webui_index_html_start");
extern const unsigned char webui_index_html_end[] asm("_binary_webui_index_html_end");
extern const unsigned char webui_script_mjs_start[] asm("_binary_webui_script_mjs_start");
extern const unsigned char webui_script_mjs_end[] asm("_binary_webui_script_mjs_end");
extern const unsigned char webui_style_css_start[] asm("_binary_webui_style_css_start");
extern const unsigned char webui_style_css_end[] asm("_binary_webui_style_css_end");

#define APP_POST_BODY_MAX_LEN 512
#define APP_OTA_BUFFER_LEN 1024

static const char *TAG = "app_web";

static esp_err_t send_embedded_file(httpd_req_t *req, const char *type, const unsigned char *start, const unsigned char *end)
{
    httpd_resp_set_type(req, type);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    size_t len = end - start;
    if (len > 0 && start[len - 1] == '\0') {
        len--;
    }
    return httpd_resp_send(req, (const char *)start, len);
}

static esp_err_t index_get_handler(httpd_req_t *req)
{
    return send_embedded_file(req, "text/html", webui_index_html_start, webui_index_html_end);
}

static esp_err_t script_get_handler(httpd_req_t *req)
{
    return send_embedded_file(req, "application/javascript", webui_script_mjs_start, webui_script_mjs_end);
}

static esp_err_t style_get_handler(httpd_req_t *req)
{
    return send_embedded_file(req, "text/css", webui_style_css_start, webui_style_css_end);
}

static const char *bool_text(bool value)
{
    return value ? "true" : "false";
}

static esp_err_t info_get_handler(httpd_req_t *req)
{
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

static esp_err_t update_post_handler(httpd_req_t *req)
{
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
    config.uri_match_fn = httpd_uri_match_wildcard;

    httpd_handle_t server = NULL;
    esp_err_t err = httpd_start(&server, &config);
    if (err != ESP_OK) {
        return err;
    }

    const httpd_uri_t routes[] = {
        {.uri = "/", .method = HTTP_GET, .handler = index_get_handler},
        {.uri = "/script.mjs", .method = HTTP_GET, .handler = script_get_handler},
        {.uri = "/style.css", .method = HTTP_GET, .handler = style_get_handler},
        {.uri = "/api/info", .method = HTTP_GET, .handler = info_get_handler},
        {.uri = "/api/wifi", .method = HTTP_GET, .handler = wifi_get_handler},
        {.uri = "/api/wifi", .method = HTTP_POST, .handler = wifi_post_handler},
        {.uri = "/update", .method = HTTP_POST, .handler = update_post_handler},
    };

    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &routes[i]), TAG, "Failed to register route");
    }

    ESP_LOGI(TAG, "Web server started");
    return ESP_OK;
}
