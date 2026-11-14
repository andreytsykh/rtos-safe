#pragma once

#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#define NFC_READER_MAX_UID_LENGTH 10U

typedef struct {
    uint8_t uid[NFC_READER_MAX_UID_LENGTH];
    uint8_t uid_length;
} nfc_reader_card_t;

typedef void (*nfc_reader_card_cb_t)(
    const nfc_reader_card_t *card,
    void *user_data
);

typedef struct {
    i2c_port_num_t i2c_port;
    uint16_t i2c_address;
    uint32_t i2c_clock_hz;
    nfc_reader_card_cb_t on_card_detected;
    void *user_data;
} nfc_reader_config_t;

/*
 * Attaches PN532 to an I2C bus already created by the application and starts
 * a task that reports each card once. The same card can be reported again
 * only after it has been removed from the reader.
 */
esp_err_t nfc_reader_init(const nfc_reader_config_t *config);
