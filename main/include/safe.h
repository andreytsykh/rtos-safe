#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "config.h"
#include "esp_err.h"

#define SAFE_CODE_LENGTH  APP_SAFE_CODE_LENGTH
#define SAFE_MAX_ATTEMPTS APP_SAFE_MAX_ATTEMPTS

typedef enum {
    SAFE_STATE_LOCKED = 0,
    SAFE_STATE_ENTERING_CODE,
    SAFE_STATE_OPEN,
    SAFE_STATE_ALARM,
} safe_state_t;

typedef enum {
    SAFE_ENTRY_VERIFY_PIN = 0,
    SAFE_ENTRY_NEW_PIN,
    SAFE_ENTRY_CONFIRM_PIN,
} safe_entry_mode_t;

typedef enum {
    SAFE_EVENT_ROTATE_CW = 0,
    SAFE_EVENT_ROTATE_CCW,
    SAFE_EVENT_BUTTON_SHORT,
    SAFE_EVENT_BUTTON_LONG,
    SAFE_EVENT_TIMEOUT,
    SAFE_EVENT_ADMIN_RESET,
} safe_event_t;

typedef struct {
    safe_state_t state;
    safe_entry_mode_t entry_mode;
    uint8_t digits[SAFE_CODE_LENGTH];
    uint8_t active_digit;
    uint8_t attempts_remaining;
} safe_snapshot_t;

typedef enum {
    SAFE_ACCESS_GRANTED = 0,
    SAFE_ACCESS_DENIED,
    SAFE_ACCESS_ALARM_TRIGGERED,
    SAFE_ACCESS_ADMIN_RESET,
} safe_access_result_t;

typedef struct {
    safe_access_result_t result;
    uint8_t attempt_number;
    uint8_t attempts_remaining;
} safe_access_attempt_t;

typedef void (*safe_update_cb_t)(const safe_snapshot_t *snapshot);
typedef void (*safe_access_attempt_cb_t)(
    const safe_access_attempt_t *attempt
);
typedef esp_err_t (*safe_pin_changed_cb_t)(
    const uint8_t pin[SAFE_CODE_LENGTH]
);

typedef struct {
    uint8_t initial_attempts_used;
    uint32_t initial_alarm_remaining_ms;
    uint8_t initial_pin[SAFE_CODE_LENGTH];
    bool initial_pin_valid;
    safe_update_cb_t on_update;
    safe_access_attempt_cb_t on_access_attempt;
    safe_pin_changed_cb_t on_pin_changed;
} safe_config_t;

/* Starts the safe state task and its event queue. */
esp_err_t safe_init(const safe_config_t *config);

/* Thread-safe, non-blocking event submission from task/timer context. */
esp_err_t safe_post_event(safe_event_t event);
