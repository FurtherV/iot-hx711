#include "app_mqtt.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mdns.h"
#include "mqtt_client.h"
#include "nvs.h"

#define APP_MQTT_NAMESPACE "mqtt_config"
#define APP_MQTT_ENABLED_KEY "enabled"
#define APP_MQTT_URI_KEY "broker_uri"
#define APP_MQTT_TOPIC_KEY "topic"
#define APP_MQTT_DISCOVERY_INTERVAL_MS 60000
#define APP_MQTT_DISCOVERY_TIMEOUT_MS 2000
#define APP_MQTT_DISCOVERY_TASK_STACK_SIZE 4096
#define APP_MQTT_DISCOVERY_TASK_PRIORITY 4

static const char *TAG = "app_mqtt";

static SemaphoreHandle_t s_mqtt_lock;
static SemaphoreHandle_t s_discovery_lock;
static esp_mqtt_client_handle_t s_client;
static TaskHandle_t s_discovery_task;
static app_mqtt_status_t s_status;
static char s_discovered_brokers[APP_MQTT_DISCOVERY_RESULT_MAX][APP_MQTT_URI_MAX_LEN + 1];
static size_t s_discovered_broker_count;

static bool mqtt_uri_valid(const char *broker_uri)
{
    return broker_uri != NULL && strncmp(broker_uri, "mqtt://", strlen("mqtt://")) == 0;
}

static void build_default_topic(char *topic, size_t topic_len)
{
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    snprintf(topic, topic_len, "iot_hx711/iot-%02X%02X%02X/sample", mac[3], mac[4], mac[5]);
}

static void set_status_text(const char *status, const char *last_error)
{
    if (xSemaphoreTake(s_mqtt_lock, portMAX_DELAY) != pdTRUE) {
        return;
    }

    strlcpy(s_status.status, status, sizeof(s_status.status));
    if (last_error != NULL) {
        strlcpy(s_status.last_error, last_error, sizeof(s_status.last_error));
    } else {
        s_status.last_error[0] = '\0';
    }

    xSemaphoreGive(s_mqtt_lock);
}

static esp_err_t load_config(void)
{
    build_default_topic(s_status.topic, sizeof(s_status.topic));
    strlcpy(s_status.status, "disabled", sizeof(s_status.status));

    nvs_handle_t nvs;
    esp_err_t err = nvs_open(APP_MQTT_NAMESPACE, NVS_READONLY, &nvs);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

    uint8_t enabled = 0;
    err = nvs_get_u8(nvs, APP_MQTT_ENABLED_KEY, &enabled);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = ESP_OK;
    }
    if (err == ESP_OK) {
        s_status.enabled = enabled != 0;
    }

    size_t broker_uri_len = sizeof(s_status.broker_uri);
    esp_err_t uri_err = nvs_get_str(nvs, APP_MQTT_URI_KEY, s_status.broker_uri, &broker_uri_len);
    if (uri_err != ESP_OK && uri_err != ESP_ERR_NVS_NOT_FOUND) {
        err = uri_err;
    }

    size_t topic_len = sizeof(s_status.topic);
    esp_err_t topic_err = nvs_get_str(nvs, APP_MQTT_TOPIC_KEY, s_status.topic, &topic_len);
    if (topic_err != ESP_OK && topic_err != ESP_ERR_NVS_NOT_FOUND) {
        err = topic_err;
    }

    nvs_close(nvs);
    return err;
}

static void apply_config_status(void)
{
    s_status.configured = mqtt_uri_valid(s_status.broker_uri) && s_status.topic[0] != '\0';
    if (!s_status.enabled) {
        strlcpy(s_status.status, "disabled", sizeof(s_status.status));
    } else if (!s_status.configured) {
        strlcpy(s_status.status, "not_configured", sizeof(s_status.status));
    } else {
        strlcpy(s_status.status, "connecting", sizeof(s_status.status));
    }
}

esp_err_t app_mqtt_save_config(bool enabled, const char *broker_uri, const char *topic)
{
    if (broker_uri == NULL || topic == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (strlen(broker_uri) > APP_MQTT_URI_MAX_LEN || strlen(topic) > APP_MQTT_TOPIC_MAX_LEN) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (enabled && (!mqtt_uri_valid(broker_uri) || topic[0] == '\0')) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs;
    esp_err_t err = nvs_open(APP_MQTT_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_u8(nvs, APP_MQTT_ENABLED_KEY, enabled ? 1 : 0);
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, APP_MQTT_URI_KEY, broker_uri);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, APP_MQTT_TOPIC_KEY, topic);
    }
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }

    nvs_close(nvs);
    return err;
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    switch (event_id) {
    case MQTT_EVENT_CONNECTED:
        if (xSemaphoreTake(s_mqtt_lock, portMAX_DELAY) == pdTRUE) {
            s_status.connected = true;
            strlcpy(s_status.status, "connected", sizeof(s_status.status));
            s_status.last_error[0] = '\0';
            xSemaphoreGive(s_mqtt_lock);
        }
        ESP_LOGI(TAG, "MQTT connected");
        break;
    case MQTT_EVENT_DISCONNECTED:
        if (xSemaphoreTake(s_mqtt_lock, portMAX_DELAY) == pdTRUE) {
            s_status.connected = false;
            strlcpy(s_status.status, s_status.enabled ? "disconnected" : "disabled", sizeof(s_status.status));
            xSemaphoreGive(s_mqtt_lock);
        }
        ESP_LOGI(TAG, "MQTT disconnected");
        break;
    case MQTT_EVENT_ERROR:
        if (xSemaphoreTake(s_mqtt_lock, portMAX_DELAY) == pdTRUE) {
            s_status.connected = false;
            strlcpy(s_status.status, "error", sizeof(s_status.status));
            strlcpy(s_status.last_error, "MQTT connection error", sizeof(s_status.last_error));
            xSemaphoreGive(s_mqtt_lock);
        }
        ESP_LOGW(TAG, "MQTT connection error");
        break;
    default:
        break;
    }
}

static esp_err_t start_client(void)
{
    if (!s_status.enabled || !s_status.configured) {
        return ESP_OK;
    }

    esp_mqtt_client_config_t config = {
        .broker.address.uri = s_status.broker_uri,
    };

    s_client = esp_mqtt_client_init(&config);
    if (s_client == NULL) {
        set_status_text("error", "Failed to initialize MQTT client");
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register MQTT event handler: %s", esp_err_to_name(err));
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
        return err;
    }
    err = esp_mqtt_client_start(s_client);
    if (err != ESP_OK) {
        set_status_text("error", "Failed to start MQTT client");
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
        return err;
    }

    ESP_LOGI(TAG, "MQTT client started for %s", s_status.broker_uri);
    return ESP_OK;
}

static bool broker_uri_seen(char broker_uris[][APP_MQTT_URI_MAX_LEN + 1], size_t broker_count, const char *broker_uri)
{
    for (size_t i = 0; i < broker_count; i++) {
        if (strcmp(broker_uris[i], broker_uri) == 0) {
            return true;
        }
    }

    return false;
}

static bool broker_uri_from_mdns_result(const mdns_result_t *result, char *broker_uri, size_t broker_uri_len)
{
    if (result == NULL || result->hostname == NULL || result->hostname[0] == '\0') {
        return false;
    }

    uint16_t port = result->port == 0 ? 1883 : result->port;
    int len = 0;
    if (strchr(result->hostname, '.') != NULL) {
        len = snprintf(broker_uri, broker_uri_len, "mqtt://%s:%u", result->hostname, port);
    } else {
        len = snprintf(broker_uri, broker_uri_len, "mqtt://%s.local:%u", result->hostname, port);
    }

    return len > 0 && len < (int)broker_uri_len;
}

static esp_err_t refresh_discovery_cache(void)
{
    mdns_result_t *results = NULL;
    esp_err_t err = mdns_query_ptr("_mqtt", "_tcp", APP_MQTT_DISCOVERY_TIMEOUT_MS, APP_MQTT_DISCOVERY_RESULT_MAX, &results);
    if (err != ESP_OK) {
        return err;
    }

    char broker_uris[APP_MQTT_DISCOVERY_RESULT_MAX][APP_MQTT_URI_MAX_LEN + 1] = {0};
    size_t broker_count = 0;

    for (const mdns_result_t *result = results; result != NULL && broker_count < APP_MQTT_DISCOVERY_RESULT_MAX; result = result->next) {
        char broker_uri[APP_MQTT_URI_MAX_LEN + 1] = {0};
        if (!broker_uri_from_mdns_result(result, broker_uri, sizeof(broker_uri))) {
            continue;
        }
        if (broker_uri_seen(broker_uris, broker_count, broker_uri)) {
            continue;
        }

        strlcpy(broker_uris[broker_count], broker_uri, sizeof(broker_uris[broker_count]));
        broker_count++;
    }

    mdns_query_results_free(results);

    if (xSemaphoreTake(s_discovery_lock, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    memset(s_discovered_brokers, 0, sizeof(s_discovered_brokers));
    for (size_t i = 0; i < broker_count; i++) {
        strlcpy(s_discovered_brokers[i], broker_uris[i], sizeof(s_discovered_brokers[i]));
    }
    s_discovered_broker_count = broker_count;

    xSemaphoreGive(s_discovery_lock);
    return ESP_OK;
}

static void mqtt_discovery_task(void *arg)
{
    while (true) {
        esp_err_t err = refresh_discovery_cache();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "MQTT DNS-SD discovery failed: %s", esp_err_to_name(err));
        }

        vTaskDelay(pdMS_TO_TICKS(APP_MQTT_DISCOVERY_INTERVAL_MS));
    }
}

static esp_err_t start_discovery_task(void)
{
    BaseType_t created = xTaskCreate(mqtt_discovery_task,
                                     "mqtt_discovery",
                                     APP_MQTT_DISCOVERY_TASK_STACK_SIZE,
                                     NULL,
                                     APP_MQTT_DISCOVERY_TASK_PRIORITY,
                                     &s_discovery_task);
    if (created != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

esp_err_t app_mqtt_start(void)
{
    if (s_mqtt_lock != NULL) {
        return ESP_OK;
    }

    s_mqtt_lock = xSemaphoreCreateMutex();
    if (s_mqtt_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }

    s_discovery_lock = xSemaphoreCreateMutex();
    if (s_discovery_lock == NULL) {
        vSemaphoreDelete(s_mqtt_lock);
        s_mqtt_lock = NULL;
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = load_config();
    if (err != ESP_OK) {
        vSemaphoreDelete(s_discovery_lock);
        vSemaphoreDelete(s_mqtt_lock);
        s_discovery_lock = NULL;
        s_mqtt_lock = NULL;
        ESP_LOGE(TAG, "Failed to load MQTT configuration: %s", esp_err_to_name(err));
        return err;
    }

    apply_config_status();

    err = start_discovery_task();
    if (err != ESP_OK) {
        vSemaphoreDelete(s_discovery_lock);
        vSemaphoreDelete(s_mqtt_lock);
        s_discovery_lock = NULL;
        s_mqtt_lock = NULL;
        return err;
    }

    return start_client();
}

void app_mqtt_get_status(app_mqtt_status_t *status)
{
    if (status == NULL) {
        return;
    }

    if (xSemaphoreTake(s_mqtt_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        memset(status, 0, sizeof(*status));
        return;
    }

    *status = s_status;
    xSemaphoreGive(s_mqtt_lock);
}

esp_err_t app_mqtt_get_discovered_brokers(char broker_uris[][APP_MQTT_URI_MAX_LEN + 1],
                                          size_t max_brokers,
                                          size_t *broker_count)
{
    if (broker_uris == NULL || broker_count == NULL || max_brokers == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    *broker_count = 0;

    if (xSemaphoreTake(s_discovery_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    size_t copy_count = s_discovered_broker_count;
    if (copy_count > max_brokers) {
        copy_count = max_brokers;
    }

    for (size_t i = 0; i < copy_count; i++) {
        strlcpy(broker_uris[i], s_discovered_brokers[i], APP_MQTT_URI_MAX_LEN + 1);
    }
    *broker_count = copy_count;

    xSemaphoreGive(s_discovery_lock);
    return ESP_OK;
}

void app_mqtt_publish_sample_json(const char *sample_json)
{
    if (sample_json == NULL || sample_json[0] == '\0' || s_mqtt_lock == NULL) {
        return;
    }

    esp_mqtt_client_handle_t client = NULL;
    char topic[APP_MQTT_TOPIC_MAX_LEN + 1] = {0};
    bool can_publish = false;

    if (xSemaphoreTake(s_mqtt_lock, pdMS_TO_TICKS(10)) == pdTRUE) {
        can_publish = s_status.enabled && s_status.connected && s_client != NULL;
        if (can_publish) {
            client = s_client;
            strlcpy(topic, s_status.topic, sizeof(topic));
        }
        xSemaphoreGive(s_mqtt_lock);
    }

    if (!can_publish) {
        return;
    }

    int msg_id = esp_mqtt_client_enqueue(client, topic, sample_json, 0, 0, 0, true);
    if (msg_id < 0) {
        ESP_LOGW(TAG, "Failed to enqueue MQTT sample");
    }
}
