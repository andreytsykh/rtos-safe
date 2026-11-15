#pragma once

#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DS1307_I2C_ADDR 0x68

typedef struct {
    uint8_t seconds;      // 0–59
    uint8_t minutes;      // 0–59
    uint8_t hours;        // 0–23
    uint8_t day_of_week;  // 1–7, домовленість: 1 = понеділок
    uint8_t day;          // 1–31
    uint8_t month;        // 1–12
    uint16_t year;        // 2000–2099
} ds1307_datetime_t;

typedef struct {
    i2c_master_dev_handle_t i2c;
} ds1307_t;

esp_err_t ds1307_init(
    ds1307_t *rtc,
    i2c_master_bus_handle_t bus
);

esp_err_t ds1307_read_datetime(
    ds1307_t *rtc,
    ds1307_datetime_t *out
);

esp_err_t ds1307_set_datetime(
    ds1307_t *rtc,
    const ds1307_datetime_t *dt
);

#ifdef __cplusplus
}
#endif
