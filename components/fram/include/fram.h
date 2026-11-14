#pragma once

#include <stddef.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#define FRAM_MB85RC256V_CAPACITY_BYTES 32768U
#define FRAM_MB85RC256V_DEFAULT_ADDRESS 0x50U

typedef struct {
    i2c_master_dev_handle_t i2c;
} fram_t;

/* The caller owns the I2C bus and keeps it alive until fram_deinit(). */
esp_err_t fram_init(
    fram_t *fram,
    i2c_master_bus_handle_t bus,
    uint16_t address,
    uint32_t clock_hz
);

esp_err_t fram_read(
    fram_t *fram,
    uint16_t address,
    void *data,
    size_t size
);

esp_err_t fram_write(
    fram_t *fram,
    uint16_t address,
    const void *data,
    size_t size
);

esp_err_t fram_deinit(fram_t *fram);
