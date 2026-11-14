#include "config.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_log.h"

#include "audio.h"
#include "app_controller.h"
#include "button.h"
#include "display.h"
#include "ds1307.h"
#include "fram.h"
#include "lock_actuator.h"
#include "nfc_reader.h"
#include "rotary_encoder.h"
#include "safe.h"
#include "safe_audit.h"
#include "safe_display.h"
#include "safe_indication.h"
#include "safe_storage.h"

static i2c_master_bus_handle_t app_i2c_bus;
static ds1307_t rtc;
#if APP_PERSISTENT_STORAGE_ENABLED
static fram_t fram;
#endif
static bool rtc_available;
static bool display_available;
#if APP_PERSISTENT_STORAGE_ENABLED
static safe_storage_state_t restored_safe_state;
#endif
static const uint8_t admin_card_uid[] = APP_NFC_ADMIN_UID;

_Static_assert(
    APP_SAFE_CODE_LENGTH == SAFE_STORAGE_PIN_LENGTH,
    "Safe and persistent PIN lengths must match"
);

static void post_safe_event(safe_event_t event)
{
    esp_err_t error = safe_post_event(event);
    if (error != ESP_OK) {
        ESP_LOGW(APP_TAG, "Failed to post safe event: %s", esp_err_to_name(error));
    }
}

static void on_button_event(button_event_t event, void *user_data)
{
    (void)user_data;

    switch (event) {
        case BUTTON_EVENT_SHORT_PRESS:
            post_safe_event(SAFE_EVENT_BUTTON_SHORT);
            break;

        case BUTTON_EVENT_LONG_PRESS:
            ESP_LOGI(APP_TAG, "Encoder button long press");
            post_safe_event(SAFE_EVENT_BUTTON_LONG);
            break;

        default:
            ESP_LOGW(APP_TAG, "Unknown button event: %d", (int)event);
            break;
    }
}

static void initialize_button(void)
{
    const button_config_t config = {
        .gpio = APP_ENCODER_SW_GPIO,
        .active_high = APP_BUTTON_ACTIVE_HIGH,
        .debounce_ms = APP_BUTTON_DEBOUNCE_MS,
        .long_press_ms = APP_BUTTON_LONG_PRESS_MS,
        .on_event = on_button_event,
        .user_data = NULL,
    };

    ESP_ERROR_CHECK(button_init(&config));
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
    esp_err_t error = display_init(app_i2c_bus);
    if (error != ESP_OK) {
        ESP_LOGW(
            APP_TAG,
            "OLED unavailable at 0x3C/0x3D; continuing without display: %s",
            esp_err_to_name(error)
        );
    } else {
        display_available = true;
    }
}

static void initialize_rtc(void)
{
    esp_err_t error = ds1307_init(&rtc, app_i2c_bus);

    if (error == ESP_OK) {
        rtc_available = true;
    } else {
        ESP_LOGW(
            APP_TAG,
            "RTC unavailable; timestamps and persistent alarm deadline disabled: %s",
            esp_err_to_name(error)
        );
    }
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

static void initialize_audit(void)
{
    const safe_audit_config_t config = {
        .rtc = rtc_available ? &rtc : NULL,
    };

    ESP_ERROR_CHECK(safe_audit_init(&config));
}

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

static void initialize_encoder(void)
{
    const rotary_encoder_config_t config = {
        .gpio_a = APP_ENCODER_A_GPIO,
        .gpio_b = APP_ENCODER_B_GPIO,
        .counts_per_detent = APP_ENCODER_COUNTS_PER_STEP,
        .glitch_filter_ns = APP_ENCODER_GLITCH_FILTER_NS,
    };

    ESP_ERROR_CHECK(rotary_encoder_init(&config));
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

static void format_uid(
    const nfc_reader_card_t *card,
    char *buffer,
    size_t buffer_size
)
{
    size_t offset = 0U;

    for (uint8_t index = 0U; index < card->uid_length; ++index) {
        int written = snprintf(
            buffer + offset,
            buffer_size - offset,
            index == 0U ? "%02X" : ":%02X",
            (unsigned int)card->uid[index]
        );
        if (written < 0 || (size_t)written >= buffer_size - offset) {
            break;
        }
        offset += (size_t)written;
    }
}

static bool card_is_admin(const nfc_reader_card_t *card)
{
    return APP_NFC_ADMIN_UID_LENGTH > 0U &&
           card->uid_length == APP_NFC_ADMIN_UID_LENGTH &&
           memcmp(
               card->uid,
               admin_card_uid,
               APP_NFC_ADMIN_UID_LENGTH
           ) == 0;
}

static void on_nfc_card_detected(
    const nfc_reader_card_t *card,
    void *user_data
)
{
    (void)user_data;

    char uid_text[NFC_READER_MAX_UID_LENGTH * 3U] = {0};
    format_uid(card, uid_text, sizeof(uid_text));

    if (APP_NFC_ADMIN_UID_LENGTH == 0U) {
        ESP_LOGI(APP_TAG, "NFC card detected: UID=%s", uid_text);
        return;
    }

    if (!card_is_admin(card)) {
        ESP_LOGW(APP_TAG, "Unauthorized NFC card: UID=%s", uid_text);
        return;
    }

    ESP_LOGI(APP_TAG, "Administrator NFC card accepted: UID=%s", uid_text);
    post_safe_event(SAFE_EVENT_ADMIN_RESET);
}

static void initialize_nfc_reader(void)
{
    const nfc_reader_config_t config = {
        .i2c_port = APP_I2C_PORT,
        .i2c_address = APP_NFC_I2C_ADDRESS,
        .i2c_clock_hz = APP_NFC_I2C_CLOCK_HZ,
        .on_card_detected = on_nfc_card_detected,
        .user_data = NULL,
    };

    esp_err_t error = nfc_reader_init(&config);
    if (error != ESP_OK) {
        ESP_LOGW(
            APP_TAG,
            "PN532 unavailable; NFC admin reset disabled: %s",
            esp_err_to_name(error)
        );
    } else if (APP_NFC_ADMIN_UID_LENGTH == 0U) {
        ESP_LOGW(
            APP_TAG,
            "Admin NFC UID is not configured; detected cards will only be logged"
        );
    }
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

static void initialize_application(void)
{
    initialize_i2c_bus();
    initialize_display();
    initialize_rtc();
#if APP_PERSISTENT_STORAGE_ENABLED
    initialize_safe_storage();
#else
    ESP_LOGW(
        APP_TAG,
        "Persistent storage disabled; security state is volatile"
    );
#endif
    initialize_safe_indication();
    initialize_encoder();
    initialize_audio();
    initialize_lock_actuator();
    initialize_audit();
    initialize_safe();
    initialize_nfc_reader();
    initialize_button();

    ESP_LOGI(APP_TAG, "Application initialized");
}

static void process_encoder_event(rotary_encoder_event_t event)
{
    switch (event) {
        case ROTARY_ENCODER_EVENT_CW:
            post_safe_event(SAFE_EVENT_ROTATE_CW);
            break;

        case ROTARY_ENCODER_EVENT_CCW:
            post_safe_event(SAFE_EVENT_ROTATE_CCW);
            break;

        case ROTARY_ENCODER_EVENT_NONE:
        default:
            break;
    }
}

static void wait_for_encoder_event(void)
{
    rotary_encoder_event_t event;
    esp_err_t error = rotary_encoder_wait_event(
        &event,
        portMAX_DELAY
    );
    if (error != ESP_OK) {
        ESP_LOGE(
            APP_TAG,
            "Failed to wait for encoder: %s",
            esp_err_to_name(error)
        );
        return;
    }

    process_encoder_event(event);
}

void app_main(void)
{
    initialize_application();

    while (true) {
        wait_for_encoder_event();
    }
}
