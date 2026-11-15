#pragma once

#include "esp_err.h"

/* Initializes encoder, NFC reader and encoder button adapters. */
esp_err_t app_input_init(void);

/* Waits for one encoder event and forwards it to the safe event queue. */
esp_err_t app_input_wait(void);
