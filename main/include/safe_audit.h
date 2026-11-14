#pragma once

#include "ds1307.h"
#include "esp_err.h"
#include "safe.h"

typedef struct {
    /* Optional: events are still stored if the RTC is unavailable. */
    ds1307_t *rtc;
} safe_audit_config_t;

/* Starts a simple sequential log in the dedicated flash partition. */
esp_err_t safe_audit_init(const safe_audit_config_t *config);

/* Non-blocking callback suitable for safe_config_t.on_access_attempt. */
void safe_audit_log_attempt(const safe_access_attempt_t *attempt);
