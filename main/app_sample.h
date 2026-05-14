#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_SAMPLE_INTERVAL_DEFAULT_MS 1000
#define APP_SAMPLE_INTERVAL_MIN_MS 100
#define APP_SAMPLE_INTERVAL_MAX_MS 10000

esp_err_t app_sample_start(void);
esp_err_t app_sample_get_json(char *buffer, size_t buffer_len);
uint32_t app_sample_get_interval_ms(void);
esp_err_t app_sample_save_interval_ms(uint32_t interval_ms);

#ifdef __cplusplus
}
#endif
