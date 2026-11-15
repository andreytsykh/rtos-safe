#include "app_input.h"

#include "config.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"

#include "button.h"
#include "esp_log.h"
#include "nfc_reader.h"
#include "rotary_encoder.h"
#include "safe.h"

static const char *TAG = "app_input";
static const uint8_t admin_card_uid[] = APP_NFC_ADMIN_UID;

static esp_err_t post_safe_event(safe_event_t event)
{
    esp_err_t error = safe_post_event(event);
    if (error != ESP_OK) {
        ESP_LOGW(TAG, "Failed to post safe event: %s", esp_err_to_name(error));
    }
    return error;
}

static void on_button_event(button_event_t event, void *user_data)
{
    (void)user_data;

    switch (event) {
        case BUTTON_EVENT_SHORT_PRESS:
            (void)post_safe_event(SAFE_EVENT_BUTTON_SHORT);
            break;

        case BUTTON_EVENT_LONG_PRESS:
            ESP_LOGI(TAG, "Encoder button long press");
            (void)post_safe_event(SAFE_EVENT_BUTTON_LONG);
            break;

        default:
            ESP_LOGW(TAG, "Unknown button event: %d", (int)event);
            break;
    }
}

static esp_err_t initialize_button(void)
{
    const button_config_t config = {
        .gpio = APP_ENCODER_SW_GPIO,
        .active_high = APP_BUTTON_ACTIVE_HIGH,
        .debounce_ms = APP_BUTTON_DEBOUNCE_MS,
        .long_press_ms = APP_BUTTON_LONG_PRESS_MS,
        .on_event = on_button_event,
        .user_data = NULL,
    };

    return button_init(&config);
}

static esp_err_t initialize_encoder(void)
{
    const rotary_encoder_config_t config = {
        .gpio_a = APP_ENCODER_A_GPIO,
        .gpio_b = APP_ENCODER_B_GPIO,
        .counts_per_detent = APP_ENCODER_COUNTS_PER_STEP,
        .glitch_filter_ns = APP_ENCODER_GLITCH_FILTER_NS,
    };

    return rotary_encoder_init(&config);
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
        ESP_LOGI(TAG, "NFC card detected: UID=%s", uid_text);
        return;
    }

    if (!card_is_admin(card)) {
        ESP_LOGW(TAG, "Unauthorized NFC card: UID=%s", uid_text);
        return;
    }

    ESP_LOGI(TAG, "Administrator NFC card accepted: UID=%s", uid_text);
    (void)post_safe_event(SAFE_EVENT_ADMIN_RESET);
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
            TAG,
            "PN532 unavailable; NFC admin reset disabled: %s",
            esp_err_to_name(error)
        );
    } else if (APP_NFC_ADMIN_UID_LENGTH == 0U) {
        ESP_LOGW(
            TAG,
            "Admin NFC UID is not configured; detected cards will only be logged"
        );
    }
}

esp_err_t app_input_init(void)
{
    esp_err_t error = initialize_encoder();
    if (error != ESP_OK) {
        return error;
    }

    initialize_nfc_reader();
    return initialize_button();
}

esp_err_t app_input_wait(void)
{
    rotary_encoder_event_t event;
    esp_err_t error = rotary_encoder_wait_event(&event, portMAX_DELAY);
    if (error != ESP_OK) {
        return error;
    }

    switch (event) {
        case ROTARY_ENCODER_EVENT_CW:
            return post_safe_event(SAFE_EVENT_ROTATE_CW);

        case ROTARY_ENCODER_EVENT_CCW:
            return post_safe_event(SAFE_EVENT_ROTATE_CCW);

        case ROTARY_ENCODER_EVENT_NONE:
        default:
            return ESP_OK;
    }
}
