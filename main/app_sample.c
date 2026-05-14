#include "app_sample.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "hx711.h"
#include "nvs.h"
#include "sdkconfig.h"

#define APP_SAMPLE_NAMESPACE "app_state"
#define APP_SAMPLE_INCARNATION_KEY "incarnation"
#define APP_SAMPLE_INTERVAL_KEY "sample_interval_ms"
#define APP_SAMPLE_JSON_MAX_LEN 256
#define APP_SAMPLE_AVERAGE_TIMES 4
#define APP_SAMPLE_TARE_OFFSET_RAW (-171000)
#define APP_SAMPLE_CALIBRATION_COUNTS 395622
#define APP_SAMPLE_CALIBRATION_GRAMS 2500
#define APP_SAMPLE_MAX_GRAMS 5000
#define APP_SAMPLE_GRAM_DECIMALS 10

static const char *TAG = "app_sample";

static SemaphoreHandle_t s_sample_lock;
static char s_sample_json[APP_SAMPLE_JSON_MAX_LEN];
static uint32_t s_incarnation;
static uint32_t s_sequence_number;
static uint32_t s_sample_interval_ms = APP_SAMPLE_INTERVAL_DEFAULT_MS;

static esp_err_t load_next_incarnation(uint32_t *incarnation)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(APP_SAMPLE_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    uint32_t current = 0;
    err = nvs_get_u32(nvs, APP_SAMPLE_INCARNATION_KEY, &current);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = ESP_OK;
    }
    if (err == ESP_OK) {
        current = current == UINT32_MAX ? 1 : current + 1;
        err = nvs_set_u32(nvs, APP_SAMPLE_INCARNATION_KEY, current);
    }
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }

    nvs_close(nvs);

    if (err == ESP_OK) {
        *incarnation = current;
    }
    return err;
}

static esp_err_t load_sample_interval(uint32_t *interval_ms)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(APP_SAMPLE_NAMESPACE, NVS_READONLY, &nvs);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        *interval_ms = APP_SAMPLE_INTERVAL_DEFAULT_MS;
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

    uint32_t stored_interval = APP_SAMPLE_INTERVAL_DEFAULT_MS;
    err = nvs_get_u32(nvs, APP_SAMPLE_INTERVAL_KEY, &stored_interval);
    nvs_close(nvs);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        stored_interval = APP_SAMPLE_INTERVAL_DEFAULT_MS;
        err = ESP_OK;
    }
    if (err == ESP_OK &&
        (stored_interval < APP_SAMPLE_INTERVAL_MIN_MS || stored_interval > APP_SAMPLE_INTERVAL_MAX_MS)) {
        ESP_LOGW(TAG, "Ignoring invalid stored sample interval: %" PRIu32 " ms", stored_interval);
        stored_interval = APP_SAMPLE_INTERVAL_DEFAULT_MS;
    }
    if (err == ESP_OK) {
        *interval_ms = stored_interval;
    }
    return err;
}

static int32_t raw_to_grams_x10(int32_t raw)
{
    int64_t net_counts = (int64_t)raw - APP_SAMPLE_TARE_OFFSET_RAW;
    int64_t grams_x10 = 0;

    if (net_counts > 0) {
        grams_x10 = ((net_counts * APP_SAMPLE_CALIBRATION_GRAMS * APP_SAMPLE_GRAM_DECIMALS) +
                     (APP_SAMPLE_CALIBRATION_COUNTS / 2)) /
                    APP_SAMPLE_CALIBRATION_COUNTS;
    }

    if (grams_x10 < 0) {
        return 0;
    }
    if (grams_x10 > APP_SAMPLE_MAX_GRAMS * APP_SAMPLE_GRAM_DECIMALS) {
        return APP_SAMPLE_MAX_GRAMS * APP_SAMPLE_GRAM_DECIMALS;
    }

    return (int32_t)grams_x10;
}

static void cache_sample_json(int32_t raw)
{
    int32_t grams_x10 = raw_to_grams_x10(raw);
    int32_t whole_grams = grams_x10 / APP_SAMPLE_GRAM_DECIMALS;
    int32_t fractional_grams = grams_x10 % APP_SAMPLE_GRAM_DECIMALS;
    char json[APP_SAMPLE_JSON_MAX_LEN];
    int len = snprintf(json, sizeof(json),
                       "{"
                       "\"incarnation\":%" PRIu32 ","
                       "\"sequence_number\":%" PRIu32 ","
                       "\"data\":["
                       "{"
                       "\"value\":%" PRIi32 ".%" PRIi32 ","
                       "\"unit\":\"g\""
                       "},"
                       "{"
                       "\"value\":%" PRIi32 ","
                       "\"unit\":\"raw\""
                       "}"
                       "]"
                       "}",
                       s_incarnation,
                       s_sequence_number,
                       whole_grams,
                       fractional_grams,
                       raw);

    if (len < 0 || len >= (int)sizeof(json)) {
        ESP_LOGE(TAG, "Sample JSON response too large");
        return;
    }

    if (xSemaphoreTake(s_sample_lock, portMAX_DELAY) == pdTRUE) {
        strlcpy(s_sample_json, json, sizeof(s_sample_json));
        xSemaphoreGive(s_sample_lock);
    }
}

static void sample_task(void *arg)
{
    hx711_t dev = {
        .dout = CONFIG_APP_HX711_DT_GPIO,
        .pd_sck = CONFIG_APP_HX711_SCK_GPIO,
        .gain = HX711_GAIN_A_64,
    };
    bool initialized = false;
    TickType_t last_wake = xTaskGetTickCount();

    while (true) {
        if (!initialized) {
            esp_err_t err = hx711_init(&dev);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "HX711 init failed: %s", esp_err_to_name(err));
                vTaskDelay(pdMS_TO_TICKS(1000));
                continue;
            }
            initialized = true;
            ESP_LOGI(TAG, "HX711 started on DT GPIO %d, SCK GPIO %d",
                     CONFIG_APP_HX711_DT_GPIO,
                     CONFIG_APP_HX711_SCK_GPIO);
            last_wake = xTaskGetTickCount();
        }

        int32_t value = 0;
        esp_err_t err = hx711_read_average(&dev, APP_SAMPLE_AVERAGE_TIMES, &value);
        if (err == ESP_OK) {
            s_sequence_number++;
            cache_sample_json(value);
        } else {
            ESP_LOGW(TAG, "HX711 sample read failed: %s", esp_err_to_name(err));
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(s_sample_interval_ms));
    }
}

esp_err_t app_sample_start(void)
{
    if (s_sample_lock != NULL) {
        return ESP_OK;
    }

    s_sample_lock = xSemaphoreCreateMutex();
    if (s_sample_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = load_next_incarnation(&s_incarnation);
    if (err != ESP_OK) {
        vSemaphoreDelete(s_sample_lock);
        s_sample_lock = NULL;
        ESP_LOGE(TAG, "Failed to load incarnation: %s", esp_err_to_name(err));
        return err;
    }
    s_sequence_number = 0;
    err = load_sample_interval(&s_sample_interval_ms);
    if (err != ESP_OK) {
        vSemaphoreDelete(s_sample_lock);
        s_sample_lock = NULL;
        ESP_LOGE(TAG, "Failed to load sample interval: %s", esp_err_to_name(err));
        return err;
    }
    cache_sample_json(APP_SAMPLE_TARE_OFFSET_RAW);

    BaseType_t created = xTaskCreate(sample_task, "sample_task", 4096, NULL, 5, NULL);
    if (created != pdPASS) {
        vSemaphoreDelete(s_sample_lock);
        s_sample_lock = NULL;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Sample service started with incarnation %" PRIu32 " and interval %" PRIu32 " ms",
             s_incarnation,
             s_sample_interval_ms);
    return ESP_OK;
}

esp_err_t app_sample_get_json(char *buffer, size_t buffer_len)
{
    if (buffer == NULL || buffer_len == 0 || s_sample_lock == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_sample_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    size_t copied = strlcpy(buffer, s_sample_json, buffer_len);
    xSemaphoreGive(s_sample_lock);

    if (copied >= buffer_len) {
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}

uint32_t app_sample_get_interval_ms(void)
{
    return s_sample_interval_ms;
}

esp_err_t app_sample_save_interval_ms(uint32_t interval_ms)
{
    if (interval_ms < APP_SAMPLE_INTERVAL_MIN_MS || interval_ms > APP_SAMPLE_INTERVAL_MAX_MS) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs;
    esp_err_t err = nvs_open(APP_SAMPLE_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_u32(nvs, APP_SAMPLE_INTERVAL_KEY, interval_ms);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err;
}
