#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BUTTON_EVENT_SHORT_PRESS = 0,
    BUTTON_EVENT_LONG_PRESS,
} button_event_t;

typedef void (*button_event_cb_t)(button_event_t event, void *user_data);

typedef struct {
    gpio_num_t gpio;
    bool active_high;
    uint32_t debounce_ms;
    uint32_t long_press_ms;
    button_event_cb_t on_event;
    void *user_data;
} button_config_t;

/* The application uses one encoder button, so the component is a singleton. */
esp_err_t button_init(const button_config_t *config);

#ifdef __cplusplus
}
#endif
