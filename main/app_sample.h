#pragma once

#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t app_sample_start(void);
esp_err_t app_sample_get_json(char *buffer, size_t buffer_len);

#ifdef __cplusplus
}
#endif
