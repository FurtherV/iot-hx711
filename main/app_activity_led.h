#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t app_activity_led_start(void);
void app_activity_led_pulse(void);

#ifdef __cplusplus
}
#endif
