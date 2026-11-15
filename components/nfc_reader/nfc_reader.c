#include "nfc_reader.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "pn532.h"

#define NFC_READER_POLL_INTERVAL_MS         250U
#define NFC_READER_RECOVERY_ERROR_THRESHOLD 3U
#define NFC_READER_TASK_STACK_SIZE          4096U
#define NFC_READER_TASK_PRIORITY            3U

static const char *TAG = "nfc_reader";

static nfc_reader_config_t reader_config;
static TaskHandle_t task_handle;
static pn532_bus_t *pn532_bus;
static pn532_t *pn532_device;

static bool cards_match(
    const nfc_reader_card_t *left,
    const pn532_uid_t *right
)
{
    return left->uid_length == (uint8_t)right->uid_length &&
           memcmp(left->uid, right->uid, left->uid_length) == 0;
}

static void notify_new_card(
    nfc_reader_card_t *last_card,
    bool *card_present,
    const pn532_uid_t *card
)
{
    if (card->uid_length <= 0 ||
        card->uid_length > (int8_t)NFC_READER_MAX_UID_LENGTH) {
        ESP_LOGW(TAG, "Ignoring card with invalid UID length");
        return;
    }

    if (*card_present && cards_match(last_card, card)) {
        return;
    }

    last_card->uid_length = (uint8_t)card->uid_length;
    memcpy(last_card->uid, card->uid, last_card->uid_length);
    *card_present = true;

    reader_config.on_card_detected(last_card, reader_config.user_data);
}

static void recover_after_transport_errors(uint8_t *error_count)
{
    if (*error_count < UINT8_MAX) {
        (*error_count)++;
    }

    if (*error_count < NFC_READER_RECOVERY_ERROR_THRESHOLD) {
        return;
    }

    ESP_LOGW(TAG, "Recovering PN532 after repeated transport errors");
    if (!pn532_recover(pn532_device)) {
        ESP_LOGE(TAG, "PN532 recovery failed");
    }
    *error_count = 0U;
}

static void handle_poll_result(
    pn532_poll_status_t status,
    const pn532_uids_array_t *cards,
    nfc_reader_card_t *last_card,
    bool *card_present,
    uint8_t *transport_error_count
)
{
    switch (status) {
        case PN532_POLL_FOUND:
            *transport_error_count = 0U;
            if (cards != NULL && cards->uids_count > 0U) {
                notify_new_card(
                    last_card,
                    card_present,
                    &cards->uids[0]
                );
            }
            break;

        case PN532_POLL_NO_TARGET:
            *transport_error_count = 0U;
            *card_present = false;
            break;

        case PN532_POLL_TRANSPORT_ERROR:
            recover_after_transport_errors(transport_error_count);
            break;

        case PN532_POLL_TIMEOUT:
            ESP_LOGD(TAG, "PN532 poll timed out");
            break;

        case PN532_POLL_PROTOCOL_ERROR:
            ESP_LOGW(TAG, "PN532 protocol error");
            break;

        case PN532_POLL_NO_MEMORY:
            ESP_LOGE(TAG, "Not enough memory to read NFC card");
            break;

        case PN532_POLL_INVALID_ARGUMENT:
        default:
            ESP_LOGE(TAG, "Invalid PN532 poll result: %d", (int)status);
            break;
    }
}

static void nfc_reader_task(void *arg)
{
    (void)arg;

    nfc_reader_card_t last_card = {0};
    bool card_present = false;
    uint8_t transport_error_count = 0U;

    while (true) {
        pn532_poll_status_t status;
        pn532_uids_array_t *cards = pn532_14443_get_all_uids_ex(
            pn532_device,
            &status
        );

        handle_poll_result(
            status,
            cards,
            &last_card,
            &card_present,
            &transport_error_count
        );

        free(cards);
        (void)pn532_set_rf_off(pn532_device);
        vTaskDelay(pdMS_TO_TICKS(NFC_READER_POLL_INTERVAL_MS));
    }
}

esp_err_t nfc_reader_init(const nfc_reader_config_t *config)
{
    if (config == NULL || config->on_card_detected == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (task_handle != NULL || pn532_bus != NULL || pn532_device != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    pn532_bus = pn532_i2c_attach(
        config->i2c_port,
        config->i2c_address,
        config->i2c_clock_hz
    );
    if (pn532_bus == NULL) {
        return ESP_ERR_NOT_FOUND;
    }

    pn532_device = pn532_init(
        pn532_bus,
        GPIO_NUM_NC,
        GPIO_NUM_NC
    );
    if (pn532_device == NULL) {
        pn532_bus_destroy(pn532_bus);
        pn532_bus = NULL;
        return ESP_ERR_NOT_FOUND;
    }

    const uint32_t firmware = pn532_get_firmware_version(pn532_device);
    ESP_LOGI(
        TAG,
        "PN532 ready: firmware=%u.%u",
        (unsigned int)((firmware >> 16U) & 0xFFU),
        (unsigned int)((firmware >> 8U) & 0xFFU)
    );

    reader_config = *config;
    BaseType_t result = xTaskCreate(
        nfc_reader_task,
        "nfc_reader",
        NFC_READER_TASK_STACK_SIZE,
        NULL,
        NFC_READER_TASK_PRIORITY,
        &task_handle
    );
    if (result != pdPASS) {
        pn532_deinit(pn532_device, true);
        pn532_device = NULL;
        pn532_bus = NULL;
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}
