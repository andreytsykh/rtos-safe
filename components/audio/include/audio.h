#pragma once

#include <stdint.h>

#include "driver/gpio.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AUDIO_SIGNAL_TICK = 0,
    AUDIO_SIGNAL_UNLOCKED,
    AUDIO_SIGNAL_ALARM,
} audio_signal_t;

typedef struct {
    gpio_num_t gpio;
} audio_config_t;

#define AUDIO_CONFIG_DEFAULT(gpio_number) \
    { .gpio = (gpio_number) }

esp_err_t audio_init(const audio_config_t *config);

/* Call from task context. Playback is asynchronous. */
esp_err_t audio_play(audio_signal_t signal);

/* Plays for at most duration_ms, even when the selected melody loops. */
esp_err_t audio_play_for(audio_signal_t signal, uint32_t duration_ms);

esp_err_t audio_stop(void);

#ifdef __cplusplus
}
#endif
