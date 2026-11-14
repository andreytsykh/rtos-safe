#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ds1307.h"
#include "esp_err.h"
#include "fram.h"

#define SAFE_STORAGE_PIN_LENGTH 4U

typedef struct {
    fram_t *fram;
    ds1307_t *rtc;
    uint8_t max_attempts;
    uint32_t alarm_duration_ms;
} safe_storage_config_t;

typedef struct {
    uint8_t attempts_used;
    uint32_t alarm_remaining_ms;
    uint8_t pin[SAFE_STORAGE_PIN_LENGTH];
    bool pin_valid;
} safe_storage_state_t;

/* Loads the state from one simple FRAM record. */
esp_err_t safe_storage_init(
    const safe_storage_config_t *config,
    safe_storage_state_t *state
);

/* Stores attempts and, when possible, an RTC-based alarm deadline. */
esp_err_t safe_storage_save(uint8_t attempts_used, bool alarm_active);

/* Stores a new PIN while preserving attempts and alarm data. */
esp_err_t safe_storage_save_pin(
    const uint8_t pin[SAFE_STORAGE_PIN_LENGTH]
);
