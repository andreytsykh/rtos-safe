#include "config.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_log.h"

#include "app_controller.h"
#include "app_input.h"
#include "audio.h"
#include "display.h"
#include "ds1307.h"
#include "fram.h"
#include "lock_actuator.h"
#include "safe.h"
#include "safe_audit.h"
#include "safe_display.h"
#include "safe_indication.h"
#include "safe_storage.h"

static i2c_master_bus_handle_t app_i2c_bus;
static ds1307_t rtc;
#if APP_PERSISTENT_STORAGE_ENABLED
static fram_t fram;
static safe_storage_state_t restored_safe_state;
#endif
static bool rtc_available;
static bool display_available;

_Static_assert(
    APP_SAFE_CODE_LENGTH == SAFE_STORAGE_PIN_LENGTH,
    "Safe and persistent PIN lengths must match"
);

static bool optional_component_available(
    const char *component,
    esp_err_t error
)
{
    if (error == ESP_OK) {
        return true;
    }

    ESP_LOGW(
        APP_TAG,
        "%s unavailable: %s",
        component,
        esp_err_to_name(error)
    );
    return false;
}

static void initialize_i2c_bus(void)
{
    const i2c_master_bus_config_t bus_config = {
        .i2c_port = APP_I2C_PORT,
        .sda_io_num = APP_I2C_SDA_GPIO,
        .scl_io_num = APP_I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7U,
        .flags = {
            .enable_internal_pullup = APP_I2C_INTERNAL_PULLUP,
        },
    };

    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &app_i2c_bus));
}

static void initialize_display(void)
{
    display_available = optional_component_available(
        "OLED at 0x3C/0x3D; continuing without display",
        display_init(app_i2c_bus)
    );
}

static void initialize_rtc(void)
{
    rtc_available = optional_component_available(
        "RTC; timestamps and persistent alarm deadline disabled",
        ds1307_init(&rtc, app_i2c_bus)
    );
}

#if APP_PERSISTENT_STORAGE_ENABLED
static void initialize_safe_storage(void)
{
    ESP_ERROR_CHECK(fram_init(
        &fram,
        app_i2c_bus,
        APP_FRAM_I2C_ADDRESS,
        APP_FRAM_I2C_CLOCK_HZ
    ));

    const safe_storage_config_t config = {
        .fram = &fram,
        .rtc = rtc_available ? &rtc : NULL,
        .max_attempts = APP_SAFE_MAX_ATTEMPTS,
        .alarm_duration_ms = APP_SAFE_ALARM_TIMEOUT_MS,
    };

    ESP_ERROR_CHECK(safe_storage_init(&config, &restored_safe_state));
}
#endif

static void initialize_safe_indication(void)
{
    const safe_indication_config_t config = {
        .gpio_red = APP_LED_RED_GPIO,
        .gpio_yellow = APP_LED_YELLOW_GPIO,
        .gpio_green = APP_LED_GREEN_GPIO,
        .active_high = APP_LED_ACTIVE_HIGH,
    };

    ESP_ERROR_CHECK(safe_indication_init(&config));
}

static void initialize_audio(void)
{
    const audio_config_t config = AUDIO_CONFIG_DEFAULT(APP_AUDIO_GPIO);
    ESP_ERROR_CHECK(audio_init(&config));
}

static void initialize_lock_actuator(void)
{
    const lock_actuator_config_t config = {
        .relay_gpio = APP_RELAY_GPIO,
        .servo_pwm_gpio = APP_SERVO_PWM_GPIO,
        .relay_active_low = APP_RELAY_CONTROL_ACTIVE_LOW,
        .locked_pulse_us = APP_SERVO_LOCKED_PULSE_US,
        .open_pulse_us = APP_SERVO_OPEN_PULSE_US,
        .power_stabilize_ms = APP_SERVO_POWER_STABILIZE_MS,
        .movement_time_ms = APP_SERVO_MOVEMENT_TIME_MS,
    };

    ESP_ERROR_CHECK(lock_actuator_init(&config));
}

static void initialize_audit(void)
{
    const safe_audit_config_t config = {
        .rtc = rtc_available ? &rtc : NULL,
    };

    ESP_ERROR_CHECK(safe_audit_init(&config));
}

static esp_err_t on_pin_changed(
    const uint8_t pin[APP_SAFE_CODE_LENGTH]
)
{
#if APP_PERSISTENT_STORAGE_ENABLED
    return safe_storage_save_pin(pin);
#else
    (void)pin;
    return ESP_OK;
#endif
}

static void initialize_safe(void)
{
    ESP_ERROR_CHECK(safe_display_init(display_available));

    const app_controller_config_t controller_config = {
#if APP_PERSISTENT_STORAGE_ENABLED
        .initial_attempts_used = restored_safe_state.attempts_used,
        .initial_alarm_active =
            restored_safe_state.alarm_remaining_ms > 0U,
#else
        .initial_attempts_used = 0U,
        .initial_alarm_active = false,
#endif
    };
    app_controller_init(&controller_config);

    safe_config_t config = {
#if APP_PERSISTENT_STORAGE_ENABLED
        .initial_attempts_used = restored_safe_state.attempts_used,
        .initial_alarm_remaining_ms =
            restored_safe_state.alarm_remaining_ms,
        .initial_pin_valid = restored_safe_state.pin_valid,
#else
        .initial_attempts_used = 0U,
        .initial_alarm_remaining_ms = 0U,
        .initial_pin_valid = false,
#endif
        .on_update = app_controller_apply,
        .on_access_attempt = safe_audit_log_attempt,
        .on_pin_changed = on_pin_changed,
    };

#if APP_PERSISTENT_STORAGE_ENABLED
    if (restored_safe_state.pin_valid) {
        memcpy(
            config.initial_pin,
            restored_safe_state.pin,
            sizeof(config.initial_pin)
        );
    }
#endif

    ESP_ERROR_CHECK(safe_init(&config));
}

static void initialize_platform(void)
{
    initialize_i2c_bus();
    initialize_rtc();
#if APP_PERSISTENT_STORAGE_ENABLED
    initialize_safe_storage();
#else
    ESP_LOGW(
        APP_TAG,
        "Persistent storage disabled; security state is volatile"
    );
    ESP_LOGI("SAFE_TEST", "SNAPSHOT_BEGIN");
    ESP_LOGI("SAFE_TEST", "RESTORED_ATTEMPTS=0");
    ESP_LOGI("SAFE_TEST", "RESTORED_STATE=LOCKED");
    ESP_LOGI("SAFE_TEST", "RTC_NOW=NOT_USED");
    ESP_LOGI("SAFE_TEST", "ALARM_DEADLINE=NONE");
    ESP_LOGI("SAFE_TEST", "REMAINING_SEC=0");
    ESP_LOGI("SAFE_TEST", "SNAPSHOT_END");
#endif
}

static void initialize_outputs(void)
{
    initialize_display();
    initialize_safe_indication();
    initialize_audio();
    initialize_lock_actuator();
}

static void initialize_services(void)
{
    initialize_audit();
    initialize_safe();
}

static void initialize_application(void)
{
    initialize_platform();
    initialize_outputs();
    initialize_services();
    ESP_ERROR_CHECK(app_input_init());
    ESP_LOGI(APP_TAG, "Application initialized");
}

void app_main(void)
{
    ESP_LOGI("SAFE_TEST", "BOOT");
    initialize_application();

    while (true) {
        esp_err_t error = app_input_wait();
        if (error != ESP_OK) {
            ESP_LOGE(
                APP_TAG,
                "Input processing failed: %s",
                esp_err_to_name(error)
            );
        }
    }
}
