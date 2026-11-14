#include "fram.h"

#include <stdbool.h>
#include <string.h>

#define FRAM_TRANSFER_TIMEOUT_MS 100U
#define FRAM_WRITE_CHUNK_SIZE    64U

static bool range_is_valid(uint16_t address, size_t size)
{
    return size <= FRAM_MB85RC256V_CAPACITY_BYTES &&
           (size_t)address <= FRAM_MB85RC256V_CAPACITY_BYTES - size;
}

esp_err_t fram_init(
    fram_t *fram,
    i2c_master_bus_handle_t bus,
    uint16_t address,
    uint32_t clock_hz
)
{
    if (fram == NULL || bus == NULL || address > 0x7FU ||
        clock_hz == 0U || clock_hz > 1000000U) {
        return ESP_ERR_INVALID_ARG;
    }

    if (fram->i2c != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t error = i2c_master_probe(
        bus,
        address,
        FRAM_TRANSFER_TIMEOUT_MS
    );
    if (error != ESP_OK) {
        return error;
    }

    const i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = clock_hz,
    };

    return i2c_master_bus_add_device(bus, &config, &fram->i2c);
}

esp_err_t fram_read(
    fram_t *fram,
    uint16_t address,
    void *data,
    size_t size
)
{
    if (fram == NULL || fram->i2c == NULL || data == NULL || size == 0U ||
        !range_is_valid(address, size)) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint8_t memory_address[] = {
        (uint8_t)(address >> 8U),
        (uint8_t)address,
    };

    return i2c_master_transmit_receive(
        fram->i2c,
        memory_address,
        sizeof(memory_address),
        data,
        size,
        FRAM_TRANSFER_TIMEOUT_MS
    );
}

esp_err_t fram_write(
    fram_t *fram,
    uint16_t address,
    const void *data,
    size_t size
)
{
    if (fram == NULL || fram->i2c == NULL || data == NULL || size == 0U ||
        !range_is_valid(address, size)) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint8_t *source = data;
    uint8_t buffer[2U + FRAM_WRITE_CHUNK_SIZE];

    while (size > 0U) {
        const size_t chunk_size = size > FRAM_WRITE_CHUNK_SIZE
                                ? FRAM_WRITE_CHUNK_SIZE
                                : size;

        buffer[0] = (uint8_t)(address >> 8U);
        buffer[1] = (uint8_t)address;
        memcpy(&buffer[2], source, chunk_size);

        esp_err_t error = i2c_master_transmit(
            fram->i2c,
            buffer,
            chunk_size + 2U,
            FRAM_TRANSFER_TIMEOUT_MS
        );
        if (error != ESP_OK) {
            return error;
        }

        address = (uint16_t)(address + chunk_size);
        source += chunk_size;
        size -= chunk_size;
    }

    return ESP_OK;
}

esp_err_t fram_deinit(fram_t *fram)
{
    if (fram == NULL || fram->i2c == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t error = i2c_master_bus_rm_device(fram->i2c);
    if (error == ESP_OK) {
        fram->i2c = NULL;
    }
    return error;
}
