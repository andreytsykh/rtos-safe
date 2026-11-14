#pragma once

#include <stdbool.h>

#include "driver/gpio.h"
#include "esp_err.h"

typedef struct {
    gpio_num_t gpio_red;
    gpio_num_t gpio_yellow;
    gpio_num_t gpio_green;
    bool active_high;
} safe_indication_config_t;

typedef enum {
    SAFE_INDICATION_OFF = 0,
    SAFE_INDICATION_RED,
    SAFE_INDICATION_YELLOW,
    SAFE_INDICATION_GREEN,
} safe_indication_color_t;

esp_err_t safe_indication_init(
    const safe_indication_config_t *config
);

esp_err_t safe_indication_set(safe_indication_color_t color);
