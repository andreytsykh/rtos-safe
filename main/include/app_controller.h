#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "safe.h"

typedef struct {
    uint8_t initial_attempts_used;
    bool initial_alarm_active;
} app_controller_config_t;

void app_controller_init(const app_controller_config_t *config);
void app_controller_apply(const safe_snapshot_t *snapshot);
