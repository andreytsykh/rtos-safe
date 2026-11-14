#include "safe_display.h"
#include "config.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_log.h"

static const char *TAG = "safe_display";
static QueueHandle_t snapshot_queue;
static TaskHandle_t task_handle;
static bool initialized;
static bool enabled;

static const display_text_style_t centered_text = {
    .scale = DISPLAY_TEXT_SCALE_1X,
    .alignment = DISPLAY_TEXT_ALIGN_CENTER,
    .inverted = false,
};

static const display_text_style_t centered_large_text = {
    .scale = DISPLAY_TEXT_SCALE_2X,
    .alignment = DISPLAY_TEXT_ALIGN_CENTER,
    .inverted = false,
};

static void log_error(const char *operation, esp_err_t error)
{
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "%s failed: %s", operation, esp_err_to_name(error));
    }
}

static void render_pin_entry(
    const safe_snapshot_t *snapshot
)
{
    const char *title;
    const char *footer;

    switch (snapshot->entry_mode) {
        case SAFE_ENTRY_NEW_PIN:
            title = "NEW PIN";
            footer = "SET NEW CODE";
            break;

        case SAFE_ENTRY_CONFIRM_PIN:
            title = "CONFIRM PIN";
            footer = "REPEAT CODE";
            break;

        case SAFE_ENTRY_VERIFY_PIN:
        default:
            title = "ENTER PIN";
            footer = NULL;
            break;
    }

    char pin[SAFE_CODE_LENGTH + 1U];
    for (size_t index = 0U; index < SAFE_CODE_LENGTH; ++index) {
        pin[index] = (index <= snapshot->active_digit)
            ? (char)('0' + snapshot->digits[index])
            : '_';
    }
    pin[SAFE_CODE_LENGTH] = '\0';

    char cursor[DISPLAY_MAX_TEXT_COLUMNS + 1U];
    memset(cursor, ' ', DISPLAY_MAX_TEXT_COLUMNS);
    cursor[DISPLAY_MAX_TEXT_COLUMNS] = '\0';

    size_t large_text_capacity = DISPLAY_MAX_TEXT_COLUMNS / 2U;
    size_t pin_offset = (large_text_capacity - SAFE_CODE_LENGTH) / 2U;
    cursor[((pin_offset + snapshot->active_digit) * 2U) + 1U] = '^';

    char attempts[DISPLAY_MAX_TEXT_COLUMNS + 1U] = {0};
    if (footer == NULL) {
        (void)snprintf(
            attempts,
            sizeof(attempts),
            "TRIES LEFT: %u",
            (unsigned int)snapshot->attempts_remaining
        );
        footer = attempts;
    }

    log_error(
        "render title",
        display_write_text(0U, title, &centered_text)
    );
    log_error(
        "render PIN",
        display_write_text(2U, pin, &centered_large_text)
    );
    log_error(
        "render cursor",
        display_write_text(4U, cursor, &centered_text)
    );
    log_error(
        "render attempts",
        display_write_text(7U, footer, &centered_text)
    );
}

static void render_message(const char *message)
{
    log_error(
        "render message",
        display_write_text(3U, message, &centered_text)
    );
}

static void render_snapshot(
    const safe_snapshot_t *snapshot,
    safe_state_t *previous_state,
    safe_entry_mode_t *previous_entry_mode,
    bool *has_previous_state
)
{
    const bool entry_mode_changed =
        snapshot->state == SAFE_STATE_ENTERING_CODE &&
        snapshot->entry_mode != *previous_entry_mode;

    if (!*has_previous_state ||
        snapshot->state != *previous_state ||
        entry_mode_changed) {
        log_error("clear display", display_clear());
        *previous_state = snapshot->state;
    }
    *previous_entry_mode = snapshot->entry_mode;
    *has_previous_state = true;

    switch (snapshot->state) {
        case SAFE_STATE_LOCKED:
            render_message("LOCKED");
            break;

        case SAFE_STATE_ENTERING_CODE:
            render_pin_entry(snapshot);
            break;

        case SAFE_STATE_OPEN:
            render_message("UNLOCKED");
            break;

        case SAFE_STATE_ALARM:
            render_message("ALARM");
            break;

        default:
            ESP_LOGW(TAG, "Unknown state: %d", (int)snapshot->state);
            break;
    }
}

static void safe_display_task(void *arg)
{
    (void)arg;
    safe_state_t previous_state = SAFE_STATE_LOCKED;
    safe_entry_mode_t previous_entry_mode = SAFE_ENTRY_VERIFY_PIN;
    bool has_previous_state = false;

    while (true) {
        safe_snapshot_t snapshot;
        if (xQueueReceive(
                snapshot_queue,
                &snapshot,
                portMAX_DELAY
            ) == pdTRUE) {
            render_snapshot(
                &snapshot,
                &previous_state,
                &previous_entry_mode,
                &has_previous_state
            );
        }
    }
}

esp_err_t safe_display_init(bool display_enabled)
{
    if (initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    initialized = true;
    enabled = display_enabled;
    if (!enabled) {
        ESP_LOGW(TAG, "Display output disabled");
        return ESP_OK;
    }

    snapshot_queue = xQueueCreate(
        APP_DISPLAY_QUEUE_LENGTH,
        sizeof(safe_snapshot_t)
    );
    if (snapshot_queue == NULL) {
        initialized = false;
        enabled = false;
        return ESP_ERR_NO_MEM;
    }

    BaseType_t result = xTaskCreate(
        safe_display_task,
        "safe_display",
        APP_DISPLAY_STACK_SIZE,
        NULL,
        APP_DISPLAY_PRIORITY,
        &task_handle
    );
    if (result != pdPASS) {
        vQueueDelete(snapshot_queue);
        snapshot_queue = NULL;
        initialized = false;
        enabled = false;
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

esp_err_t safe_display_post(const safe_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!enabled) {
        return ESP_OK;
    }

    return (xQueueOverwrite(snapshot_queue, snapshot) == pdPASS)
        ? ESP_OK
        : ESP_FAIL;
}
