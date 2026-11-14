#include "safe.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/timers.h"

#include "esp_log.h"

typedef enum {
    SAFE_DIRECTION_NONE = 0,
    SAFE_DIRECTION_CW,
    SAFE_DIRECTION_CCW,
} safe_direction_t;

typedef struct {
    safe_state_t state;
    safe_entry_mode_t entry_mode;
    uint8_t code[SAFE_CODE_LENGTH];
    uint8_t digit_index;
    uint8_t current_digit;
    uint8_t attempts_used;
    uint8_t secret_code[SAFE_CODE_LENGTH];
    uint8_t pending_new_code[SAFE_CODE_LENGTH];
    safe_direction_t current_direction;
    safe_update_cb_t on_update;
    safe_access_attempt_cb_t on_access_attempt;
    safe_pin_changed_cb_t on_pin_changed;
    uint32_t next_alarm_timeout_ms;
} safe_context_t;

static const char *TAG = "safe";
static const uint8_t DEFAULT_CODE[SAFE_CODE_LENGTH] = {4U, 2U, 7U, 1U};

static QueueHandle_t event_queue;
static TaskHandle_t task_handle;
static TimerHandle_t state_timer;
static safe_config_t task_config;

static void reset_input(safe_context_t *context)
{
    memset(context->code, 0, sizeof(context->code));
    context->digit_index = 0U;
    context->current_digit = 0U;
    context->current_direction = SAFE_DIRECTION_NONE;
}

static void publish_update(const safe_context_t *context)
{
    safe_snapshot_t snapshot = {
        .state = context->state,
        .entry_mode = context->entry_mode,
        .active_digit = context->digit_index,
        .attempts_remaining =
            (uint8_t)(SAFE_MAX_ATTEMPTS - context->attempts_used),
    };

    memcpy(snapshot.digits, context->code, sizeof(snapshot.digits));
    snapshot.digits[context->digit_index] = context->current_digit;
    context->on_update(&snapshot);
}

static void start_state_timer(uint32_t timeout_ms)
{
    (void)xTimerChangePeriod(
        state_timer,
        pdMS_TO_TICKS(timeout_ms),
        0U
    );
}

static void enter_state(safe_context_t *context, safe_state_t state)
{
    (void)xTimerStop(state_timer, 0U);
    context->state = state;

    switch (state) {
        case SAFE_STATE_LOCKED:
            context->entry_mode = SAFE_ENTRY_VERIFY_PIN;
            reset_input(context);
            break;

        case SAFE_STATE_ENTERING_CODE:
            reset_input(context);
            start_state_timer(APP_SAFE_ENTRY_TIMEOUT_MS);
            break;

        case SAFE_STATE_OPEN:
            start_state_timer(APP_SAFE_OPEN_TIMEOUT_MS);
            break;

        case SAFE_STATE_ALARM:
            start_state_timer(context->next_alarm_timeout_ms);
            context->next_alarm_timeout_ms = APP_SAFE_ALARM_TIMEOUT_MS;
            break;

        default:
            ESP_LOGW(TAG, "Unknown state: %d", (int)state);
            return;
    }

    publish_update(context);
}

static void publish_access_attempt(
    const safe_context_t *context,
    safe_access_result_t result
)
{
    if (context->on_access_attempt == NULL) {
        return;
    }

    const safe_access_attempt_t attempt = {
        .result = result,
        .attempt_number = context->attempts_used,
        .attempts_remaining =
            (uint8_t)(SAFE_MAX_ATTEMPTS - context->attempts_used),
    };
    context->on_access_attempt(&attempt);
}

static bool code_matches(const safe_context_t *context)
{
    return memcmp(context->code, context->secret_code, SAFE_CODE_LENGTH) == 0;
}

static void fail_attempt(safe_context_t *context)
{
    context->attempts_used++;
    ESP_LOGW(
        TAG,
        "Incorrect PIN (%u/%u)",
        context->attempts_used,
        SAFE_MAX_ATTEMPTS
    );

    if (context->attempts_used >= SAFE_MAX_ATTEMPTS) {
        publish_access_attempt(context, SAFE_ACCESS_ALARM_TRIGGERED);
        enter_state(context, SAFE_STATE_ALARM);
        return;
    }

    publish_access_attempt(context, SAFE_ACCESS_DENIED);
    reset_input(context);
    publish_update(context);
}

static void restart_pin_entry(safe_context_t *context)
{
    reset_input(context);
    start_state_timer(APP_SAFE_ENTRY_TIMEOUT_MS);
    publish_update(context);
}

static void finish_new_pin_entry(safe_context_t *context)
{
    memcpy(
        context->pending_new_code,
        context->code,
        sizeof(context->pending_new_code)
    );
    context->entry_mode = SAFE_ENTRY_CONFIRM_PIN;
    ESP_LOGI(TAG, "New PIN entered; waiting for confirmation");
    restart_pin_entry(context);
}

static void finish_pin_confirmation(safe_context_t *context)
{
    if (memcmp(
            context->code,
            context->pending_new_code,
            SAFE_CODE_LENGTH
        ) != 0) {
        ESP_LOGW(TAG, "PIN confirmation mismatch");
        context->entry_mode = SAFE_ENTRY_NEW_PIN;
        restart_pin_entry(context);
        return;
    }

    if (context->on_pin_changed == NULL) {
        ESP_LOGE(TAG, "Cannot change PIN: storage callback is missing");
        context->entry_mode = SAFE_ENTRY_NEW_PIN;
        restart_pin_entry(context);
        return;
    }

    esp_err_t error = context->on_pin_changed(context->pending_new_code);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "Cannot save PIN: %s", esp_err_to_name(error));
        context->entry_mode = SAFE_ENTRY_NEW_PIN;
        restart_pin_entry(context);
        return;
    }

    memcpy(
        context->secret_code,
        context->pending_new_code,
        sizeof(context->secret_code)
    );
    context->attempts_used = 0U;
    ESP_LOGI(TAG, "PIN changed successfully");
    enter_state(context, SAFE_STATE_LOCKED);
}

static void confirm_digit(safe_context_t *context)
{
    context->code[context->digit_index] = context->current_digit;

    if (context->digit_index == SAFE_CODE_LENGTH - 1U) {
        if (context->entry_mode == SAFE_ENTRY_NEW_PIN) {
            finish_new_pin_entry(context);
        } else if (context->entry_mode == SAFE_ENTRY_CONFIRM_PIN) {
            finish_pin_confirmation(context);
        } else if (code_matches(context)) {
            context->attempts_used++;
            publish_access_attempt(context, SAFE_ACCESS_GRANTED);
            context->attempts_used = 0U;
            enter_state(context, SAFE_STATE_OPEN);
        } else {
            fail_attempt(context);
        }
        return;
    }

    context->digit_index++;
    context->current_digit = 0U;
    publish_update(context);
}

static void handle_rotation(
    safe_context_t *context,
    safe_direction_t direction
)
{
    if (context->current_direction == SAFE_DIRECTION_NONE ||
        context->current_direction == direction) {
        context->current_direction = direction;
        context->current_digit =
            (uint8_t)((context->current_digit + 1U) % 10U);
        publish_update(context);
        return;
    }

    context->current_direction = direction;
    confirm_digit(context);
}

static void handle_locked_event(safe_context_t *context, safe_event_t event)
{
    if (event == SAFE_EVENT_BUTTON_SHORT) {
        context->entry_mode = SAFE_ENTRY_VERIFY_PIN;
        enter_state(context, SAFE_STATE_ENTERING_CODE);
    } else if (event == SAFE_EVENT_ROTATE_CW ||
               event == SAFE_EVENT_ROTATE_CCW) {
        context->entry_mode = SAFE_ENTRY_VERIFY_PIN;
        enter_state(context, SAFE_STATE_ENTERING_CODE);
        handle_rotation(
            context,
            event == SAFE_EVENT_ROTATE_CW
                ? SAFE_DIRECTION_CW
                : SAFE_DIRECTION_CCW
        );
    }
}

static void handle_entering_code_event(
    safe_context_t *context,
    safe_event_t event
)
{
    switch (event) {
        case SAFE_EVENT_ROTATE_CW:
            handle_rotation(context, SAFE_DIRECTION_CW);
            break;

        case SAFE_EVENT_ROTATE_CCW:
            handle_rotation(context, SAFE_DIRECTION_CCW);
            break;

        case SAFE_EVENT_BUTTON_SHORT:
            restart_pin_entry(context);
            break;

        case SAFE_EVENT_BUTTON_LONG:
        case SAFE_EVENT_TIMEOUT:
            enter_state(context, SAFE_STATE_LOCKED);
            break;

        default:
            break;
    }
}

static void handle_open_event(safe_context_t *context, safe_event_t event)
{
    if (event == SAFE_EVENT_BUTTON_SHORT || event == SAFE_EVENT_TIMEOUT) {
        context->attempts_used = 0U;
        enter_state(context, SAFE_STATE_LOCKED);
    } else if (event == SAFE_EVENT_BUTTON_LONG) {
        ESP_LOGI(TAG, "Starting new PIN entry");
        context->entry_mode = SAFE_ENTRY_NEW_PIN;
        enter_state(context, SAFE_STATE_ENTERING_CODE);
    }
}

static void handle_alarm_event(safe_context_t *context, safe_event_t event)
{
    if (event == SAFE_EVENT_TIMEOUT) {
        context->attempts_used = 0U;
        enter_state(context, SAFE_STATE_LOCKED);
    }
}

static void process_event(safe_context_t *context, safe_event_t event)
{
    if (event == SAFE_EVENT_ADMIN_RESET) {
        if (context->state == SAFE_STATE_ALARM) {
            publish_access_attempt(context, SAFE_ACCESS_ADMIN_RESET);
            context->attempts_used = 0U;
            ESP_LOGI(TAG, "Alarm reset by administrator card");
            enter_state(context, SAFE_STATE_LOCKED);
        }
        return;
    }

    switch (context->state) {
        case SAFE_STATE_LOCKED:
            handle_locked_event(context, event);
            break;
        case SAFE_STATE_ENTERING_CODE:
            handle_entering_code_event(context, event);
            break;
        case SAFE_STATE_OPEN:
            handle_open_event(context, event);
            break;
        case SAFE_STATE_ALARM:
            handle_alarm_event(context, event);
            break;
        default:
            break;
    }
}

static void timer_callback(TimerHandle_t timer)
{
    (void)timer;
    (void)safe_post_event(SAFE_EVENT_TIMEOUT);
}

static void safe_task(void *arg)
{
    const safe_config_t *config = arg;
    safe_context_t context = {
        .state = SAFE_STATE_LOCKED,
        .attempts_used = config->initial_attempts_used,
        .on_update = config->on_update,
        .on_access_attempt = config->on_access_attempt,
        .on_pin_changed = config->on_pin_changed,
        .next_alarm_timeout_ms = config->initial_alarm_remaining_ms > 0U
                               ? config->initial_alarm_remaining_ms
                               : APP_SAFE_ALARM_TIMEOUT_MS,
    };

    memcpy(context.secret_code, DEFAULT_CODE, sizeof(context.secret_code));
    if (config->initial_pin_valid) {
        memcpy(context.secret_code, config->initial_pin, SAFE_CODE_LENGTH);
    }

    enter_state(
        &context,
        config->initial_alarm_remaining_ms > 0U
            ? SAFE_STATE_ALARM
            : SAFE_STATE_LOCKED
    );
    ESP_LOGI(TAG, "Safe task started");

    while (true) {
        safe_event_t event;
        if (xQueueReceive(event_queue, &event, portMAX_DELAY) == pdTRUE) {
            process_event(&context, event);
        }
    }
}

esp_err_t safe_init(const safe_config_t *config)
{
    if (config == NULL || config->on_update == NULL ||
        config->initial_attempts_used > SAFE_MAX_ATTEMPTS) {
        return ESP_ERR_INVALID_ARG;
    }
    if (event_queue != NULL || task_handle != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    event_queue = xQueueCreate(
        APP_SAFE_EVENT_QUEUE_LENGTH,
        sizeof(safe_event_t)
    );
    state_timer = xTimerCreate(
        "safe_state",
        pdMS_TO_TICKS(APP_SAFE_ENTRY_TIMEOUT_MS),
        pdFALSE,
        NULL,
        timer_callback
    );
    if (event_queue == NULL || state_timer == NULL) {
        return ESP_ERR_NO_MEM;
    }

    task_config = *config;
    BaseType_t result = xTaskCreate(
        safe_task,
        "safe",
        APP_SAFE_TASK_STACK_SIZE,
        &task_config,
        APP_SAFE_TASK_PRIORITY,
        &task_handle
    );
    return result == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t safe_post_event(safe_event_t event)
{
    if (event_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (event < SAFE_EVENT_ROTATE_CW || event > SAFE_EVENT_ADMIN_RESET) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xQueueSend(event_queue, &event, 0U) != pdTRUE) {
        ESP_LOGW(TAG, "Event queue full; dropped event %d", (int)event);
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}
