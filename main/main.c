#include <stdio.h>
#include <inttypes.h>
#include <stdbool.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_system.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "nvs_flash.h"
#include "hx711.h"
#include "app_activity_led.h"
#include "app_mdns.h"
#include "app_sample.h"
#include "app_web.h"
#include "app_wifi.h"

static const char *TAG = "main";

#define APP_OTA_VALIDATION_DELAY_MS 60000

void show_greetings(void)
{
    printf("Hello world!\n");

    /* Print chip information */
    esp_chip_info_t chip_info;
    uint32_t flash_size;
    esp_chip_info(&chip_info);
    printf("This is %s chip with %d CPU core(s), %s%s%s%s, ",
           CONFIG_IDF_TARGET,
           chip_info.cores,
           (chip_info.features & CHIP_FEATURE_WIFI_BGN) ? "WiFi/" : "",
           (chip_info.features & CHIP_FEATURE_BT) ? "BT" : "",
           (chip_info.features & CHIP_FEATURE_BLE) ? "BLE" : "",
           (chip_info.features & CHIP_FEATURE_IEEE802154) ? ", 802.15.4 (Zigbee/Thread)" : "");

    unsigned major_rev = chip_info.revision / 100;
    unsigned minor_rev = chip_info.revision % 100;
    printf("silicon revision v%d.%d, ", major_rev, minor_rev);
    if(esp_flash_get_size(NULL, &flash_size) != ESP_OK) {
        printf("Get flash size failed");
        return;
    }

    printf("%" PRIu32 "MB %s flash\n", flash_size / (uint32_t)(1024 * 1024),
           (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");

    printf("Minimum free heap size: %" PRIu32 " bytes\n", esp_get_minimum_free_heap_size());
}

static bool running_app_pending_verify(void)
{
#ifdef CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    esp_err_t err = esp_ota_get_state_partition(running, &state);

    if (err == ESP_ERR_NOT_FOUND) {
        return false;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read OTA state: %s", esp_err_to_name(err));
        return false;
    }

    return state == ESP_OTA_IMG_PENDING_VERIFY;
#else
    return false;
#endif
}

static void mark_running_app_valid_task(void *arg)
{
#ifdef CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
    ESP_LOGI(TAG, "OTA image pending verification; validating after %d ms", APP_OTA_VALIDATION_DELAY_MS);
    vTaskDelay(pdMS_TO_TICKS(APP_OTA_VALIDATION_DELAY_MS));

    if (running_app_pending_verify()) {
        esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "OTA image marked valid; rollback cancelled");
        } else {
            ESP_LOGE(TAG, "Failed to mark OTA image valid: %s", esp_err_to_name(err));
        }
    }
#endif

    vTaskDelete(NULL);
}

static void start_ota_validation_guard(void)
{
#ifdef CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
    if (!running_app_pending_verify()) {
        return;
    }

    BaseType_t created = xTaskCreate(mark_running_app_valid_task, "ota_valid_task", 4096, NULL, 5, NULL);
    if (created != pdPASS) {
        ESP_LOGE(TAG, "Failed to start OTA validation task");
    }
#endif
}

void app_main(void)
{
    show_greetings();

    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_err);

    ESP_ERROR_CHECK(app_activity_led_start());
    ESP_ERROR_CHECK(app_sample_start());
    ESP_ERROR_CHECK(app_wifi_start());
    ESP_ERROR_CHECK(app_mdns_start());
    ESP_ERROR_CHECK(app_web_start());
    start_ota_validation_guard();

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
