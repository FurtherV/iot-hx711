#include "app_activity_led.h"

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#define APP_ACTIVITY_LED_GPIO ((gpio_num_t)CONFIG_APP_STATUS_LED_GPIO)
#define APP_ACTIVITY_LED_TASK_STACK_SIZE 2048
#define APP_ACTIVITY_LED_TASK_PRIORITY 5
#define APP_ACTIVITY_LED_ON_MS 60
#define APP_ACTIVITY_LED_ON_LEVEL 0
#define APP_ACTIVITY_LED_OFF_LEVEL 1

static const char *TAG = "app_activity_led";
static TaskHandle_t s_activity_led_task;

static void activity_led_task(void *arg)
{
    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        gpio_set_level(APP_ACTIVITY_LED_GPIO, APP_ACTIVITY_LED_ON_LEVEL);
        vTaskDelay(pdMS_TO_TICKS(APP_ACTIVITY_LED_ON_MS));
        gpio_set_level(APP_ACTIVITY_LED_GPIO, APP_ACTIVITY_LED_OFF_LEVEL);

        while (ulTaskNotifyTake(pdTRUE, 0) > 0) {
            gpio_set_level(APP_ACTIVITY_LED_GPIO, APP_ACTIVITY_LED_ON_LEVEL);
            vTaskDelay(pdMS_TO_TICKS(APP_ACTIVITY_LED_ON_MS));
            gpio_set_level(APP_ACTIVITY_LED_GPIO, APP_ACTIVITY_LED_OFF_LEVEL);
            vTaskDelay(pdMS_TO_TICKS(APP_ACTIVITY_LED_ON_MS));
        }
    }
}

esp_err_t app_activity_led_start(void)
{
    gpio_reset_pin(APP_ACTIVITY_LED_GPIO);
    ESP_RETURN_ON_ERROR(gpio_set_direction(APP_ACTIVITY_LED_GPIO, GPIO_MODE_OUTPUT),
                        TAG, "Failed to configure activity LED GPIO");
    ESP_RETURN_ON_ERROR(gpio_set_level(APP_ACTIVITY_LED_GPIO, APP_ACTIVITY_LED_OFF_LEVEL),
                        TAG, "Failed to clear activity LED GPIO");

    BaseType_t task_created = xTaskCreate(activity_led_task,
                                         "activity_led",
                                         APP_ACTIVITY_LED_TASK_STACK_SIZE,
                                         NULL,
                                         APP_ACTIVITY_LED_TASK_PRIORITY,
                                         &s_activity_led_task);
    if (task_created != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Activity LED started on GPIO %d", CONFIG_APP_STATUS_LED_GPIO);
    return ESP_OK;
}

void app_activity_led_pulse(void)
{
    if (s_activity_led_task == NULL) {
        return;
    }

    xTaskNotifyGive(s_activity_led_task);
}
