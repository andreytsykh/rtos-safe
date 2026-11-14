#pragma once

#include <stdint.h>

#include "driver/gpio.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ROTARY_ENCODER_EVENT_NONE = 0,
    ROTARY_ENCODER_EVENT_CW,
    ROTARY_ENCODER_EVENT_CCW,
} rotary_encoder_event_t;

typedef struct {
    gpio_num_t gpio_a;
    gpio_num_t gpio_b;
    int32_t counts_per_detent;
    uint32_t glitch_filter_ns;
} rotary_encoder_config_t;

esp_err_t rotary_encoder_init(const rotary_encoder_config_t *config);

/* Blocks only the calling task until an encoder event or timeout. */
esp_err_t rotary_encoder_wait_event(
    rotary_encoder_event_t *out_event,
    TickType_t timeout
);

esp_err_t rotary_encoder_reset(void);

#ifdef __cplusplus
}
#endif
