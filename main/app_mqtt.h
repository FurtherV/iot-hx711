#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_MQTT_URI_MAX_LEN 128
#define APP_MQTT_TOPIC_MAX_LEN 128
#define APP_MQTT_STATUS_MAX_LEN 24
#define APP_MQTT_ERROR_MAX_LEN 80
#define APP_MQTT_DISCOVERY_RESULT_MAX 16

typedef struct {
    bool enabled;
    bool configured;
    bool connected;
    char broker_uri[APP_MQTT_URI_MAX_LEN + 1];
    char topic[APP_MQTT_TOPIC_MAX_LEN + 1];
    char status[APP_MQTT_STATUS_MAX_LEN];
    char last_error[APP_MQTT_ERROR_MAX_LEN];
} app_mqtt_status_t;

esp_err_t app_mqtt_start(void);
esp_err_t app_mqtt_save_config(bool enabled, const char *broker_uri, const char *topic);
void app_mqtt_get_status(app_mqtt_status_t *status);
esp_err_t app_mqtt_get_discovered_brokers(char broker_uris[][APP_MQTT_URI_MAX_LEN + 1],
                                          size_t max_brokers,
                                          size_t *broker_count);
void app_mqtt_publish_sample_json(const char *sample_json);

#ifdef __cplusplus
}
#endif
