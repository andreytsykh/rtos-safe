#include "safe_indication.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static safe_indication_config_t indication_config;
static bool initialized;

static esp_err_t set_led(gpio_num_t gpio, bool enabled)
{
    uint32_t level = (enabled == indication_config.active_high) ? 1U : 0U;
    return gpio_set_level(gpio, level);
}

esp_err_t safe_indication_init(
    const safe_indication_config_t *config
)
{
    if (config == NULL ||
        !GPIO_IS_VALID_OUTPUT_GPIO(config->gpio_red) ||
        !GPIO_IS_VALID_OUTPUT_GPIO(config->gpio_yellow) ||
        !GPIO_IS_VALID_OUTPUT_GPIO(config->gpio_green) ||
        config->gpio_red == config->gpio_yellow ||
        config->gpio_red == config->gpio_green ||
        config->gpio_yellow == config->gpio_green) {
        return ESP_ERR_INVALID_ARG;
    }

    if (initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    indication_config = *config;

    const gpio_config_t gpio_configuration = {
        .pin_bit_mask = (1ULL << config->gpio_red) |
                        (1ULL << config->gpio_yellow) |
                        (1ULL << config->gpio_green),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t error = gpio_config(&gpio_configuration);
    if (error != ESP_OK) {
        return error;
    }

    initialized = true;
    return safe_indication_set(SAFE_INDICATION_OFF);
}

esp_err_t safe_indication_set(safe_indication_color_t color)
{
    if (!initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (color < SAFE_INDICATION_OFF ||
        color > SAFE_INDICATION_GREEN) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t error = set_led(indication_config.gpio_red, false);
    if (error == ESP_OK) {
        error = set_led(indication_config.gpio_yellow, false);
    }
    if (error == ESP_OK) {
        error = set_led(indication_config.gpio_green, false);
    }
    if (error != ESP_OK || color == SAFE_INDICATION_OFF) {
        return error;
    }

    gpio_num_t active_gpio = indication_config.gpio_red;

    if (color == SAFE_INDICATION_YELLOW) {
        active_gpio = indication_config.gpio_yellow;
    } else if (color == SAFE_INDICATION_GREEN) {
        active_gpio = indication_config.gpio_green;
    }

    return set_led(active_gpio, true);
}
