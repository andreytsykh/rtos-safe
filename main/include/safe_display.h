#pragma once

#include <stdbool.h>

#include "display.h"
#include "esp_err.h"
#include "safe.h"

esp_err_t safe_display_init(bool enabled);

/* Keeps only the newest snapshot. Call from task context. */
esp_err_t safe_display_post(const safe_snapshot_t *snapshot);
