#include "app_controller.h"

#include "audio.h"
#include "config.h"
#include "esp_log.h"
#include "lock_actuator.h"
#include "safe_display.h"
#include "safe_indication.h"
#include "safe_storage.h"

static const char *TAG = "app_controller";
static safe_state_t previous_state;
static bool has_previous_state;
static uint8_t stored_attempts;
static bool stored_alarm;

static void log_error(const char *operation, esp_err_t error)
{
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "%s failed: %s", operation, esp_err_to_name(error));
    }
}

void app_controller_init(const app_controller_config_t *config)
{
    stored_attempts = config->initial_attempts_used;
    stored_alarm = config->initial_alarm_active;
}

static void update_persistent_state(const safe_snapshot_t *snapshot)
{
#if APP_PERSISTENT_STORAGE_ENABLED
    const uint8_t attempts_used =
        (uint8_t)(SAFE_MAX_ATTEMPTS - snapshot->attempts_remaining);
    const bool alarm_active = snapshot->state == SAFE_STATE_ALARM;

    if (attempts_used == stored_attempts && alarm_active == stored_alarm) {
        return;
    }

    esp_err_t error = safe_storage_save(attempts_used, alarm_active);
    if (error == ESP_OK) {
        stored_attempts = attempts_used;
        stored_alarm = alarm_active;
    } else {
        log_error("save state", error);
    }
#else
    (void)snapshot;
#endif
}

static void update_actuator(safe_state_t state)
{
    if (state == SAFE_STATE_OPEN) {
        log_error(
            "open lock",
            lock_actuator_set_position(LOCK_ACTUATOR_POSITION_OPEN)
        );
    } else if (state == SAFE_STATE_LOCKED || state == SAFE_STATE_ALARM) {
        log_error(
            "lock",
            lock_actuator_set_position(LOCK_ACTUATOR_POSITION_LOCKED)
        );
    }
}

static void update_feedback(safe_state_t state)
{
    switch (state) {
        case SAFE_STATE_LOCKED:
            log_error("stop audio", audio_stop());
            log_error("red LED", safe_indication_set(SAFE_INDICATION_RED));
            break;
        case SAFE_STATE_ENTERING_CODE:
            log_error("stop audio", audio_stop());
            log_error(
                "yellow LED",
                safe_indication_set(SAFE_INDICATION_YELLOW)
            );
            break;
        case SAFE_STATE_OPEN:
            log_error("green LED", safe_indication_set(SAFE_INDICATION_GREEN));
            log_error("success sound", audio_play(AUDIO_SIGNAL_UNLOCKED));
            break;
        case SAFE_STATE_ALARM:
            log_error("red LED", safe_indication_set(SAFE_INDICATION_RED));
            log_error(
                "alarm sound",
                audio_play_for(
                    AUDIO_SIGNAL_ALARM,
                    APP_ALARM_SOUND_DURATION_MS
                )
            );
            break;
    }
}

void app_controller_apply(const safe_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }

    log_error("display update", safe_display_post(snapshot));
    update_persistent_state(snapshot);

    if (has_previous_state && snapshot->state == previous_state) {
        return;
    }

    previous_state = snapshot->state;
    has_previous_state = true;
    update_feedback(snapshot->state);
    update_actuator(snapshot->state);
}
