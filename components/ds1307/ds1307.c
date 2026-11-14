#include "ds1307.h"

#include <stdbool.h>
#include <stddef.h>

#define DS1307_IO_TIMEOUT_MS 1000

#define DS1307_REG_SECONDS 0x00
#define DS1307_CH_BIT      0x80
#define DS1307_12H_BIT     0x40
#define DS1307_PM_BIT      0x20

static uint8_t dec_to_bcd(uint8_t value)
{
    return (uint8_t)(((value / 10U) << 4U) | (value % 10U));
}

static bool bcd_to_dec(uint8_t bcd, uint8_t *out)
{
    uint8_t tens = bcd >> 4U;
    uint8_t units = bcd & 0x0FU;

    if (tens > 9U || units > 9U) {
        return false;
    }

    *out = (uint8_t)(tens * 10U + units);
    return true;
}

static bool is_leap_year(uint16_t year)
{
    return (year % 4U == 0U) &&
           ((year % 100U != 0U) || (year % 400U == 0U));
}

static bool datetime_is_valid(const ds1307_datetime_t *dt)
{
    if (!dt) {
        return false;
    }

    if (dt->year < 2000U || dt->year > 2099U ||
        dt->month < 1U || dt->month > 12U ||
        dt->day_of_week < 1U || dt->day_of_week > 7U ||
        dt->hours > 23U ||
        dt->minutes > 59U ||
        dt->seconds > 59U) {
        return false;
    }

    static const uint8_t days_in_month[] = {
        31, 28, 31, 30, 31, 30,
        31, 31, 30, 31, 30, 31
    };

    uint8_t max_day = days_in_month[dt->month - 1U];

    if (dt->month == 2U && is_leap_year(dt->year)) {
        max_day = 29U;
    }

    return dt->day >= 1U && dt->day <= max_day;
}

esp_err_t ds1307_init(
    ds1307_t *rtc,
    i2c_master_bus_handle_t bus)
{
    if (!rtc || !bus) {
        return ESP_ERR_INVALID_ARG;
    }

    if (rtc->i2c) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = i2c_master_probe(
        bus,
        DS1307_I2C_ADDR,
        DS1307_IO_TIMEOUT_MS
    );

    if (err != ESP_OK) {
        return err;
    }

    const i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = DS1307_I2C_ADDR,
        .scl_speed_hz = 100000,
    };

    return i2c_master_bus_add_device(
        bus,
        &config,
        &rtc->i2c
    );
}

esp_err_t ds1307_read_datetime(
    ds1307_t *rtc,
    ds1307_datetime_t *out)
{
    if (!rtc || !out) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!rtc->i2c) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t reg = DS1307_REG_SECONDS;
    uint8_t data[7];

    esp_err_t err = i2c_master_transmit_receive(
        rtc->i2c,
        &reg,
        sizeof(reg),
        data,
        sizeof(data),
        DS1307_IO_TIMEOUT_MS
    );

    if (err != ESP_OK) {
        return err;
    }

    if (data[0] & DS1307_CH_BIT) {
        return ESP_ERR_INVALID_STATE;
    }

    ds1307_datetime_t result = {0};
    uint8_t year;

    if (!bcd_to_dec(data[0] & 0x7FU, &result.seconds) ||
        !bcd_to_dec(data[1] & 0x7FU, &result.minutes) ||
        !bcd_to_dec(data[4] & 0x3FU, &result.day) ||
        !bcd_to_dec(data[5] & 0x1FU, &result.month) ||
        !bcd_to_dec(data[6], &year)) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    result.day_of_week = data[3] & 0x07U;
    result.year = 2000U + year;

    if (data[2] & DS1307_12H_BIT) {
        uint8_t hours_12;

        if (!bcd_to_dec(data[2] & 0x1FU, &hours_12) ||
            hours_12 < 1U || hours_12 > 12U) {
            return ESP_ERR_INVALID_RESPONSE;
        }

        // 12 AM → 0; 12 PM → 12.
        result.hours = hours_12 % 12U;

        if (data[2] & DS1307_PM_BIT) {
            result.hours += 12U;
        }
    } else {
        if (!bcd_to_dec(data[2] & 0x3FU, &result.hours)) {
            return ESP_ERR_INVALID_RESPONSE;
        }
    }

    if (!datetime_is_valid(&result)) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    *out = result;
    return ESP_OK;
}

esp_err_t ds1307_set_datetime(
    ds1307_t *rtc,
    const ds1307_datetime_t *dt)
{
    if (!rtc || !dt) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!rtc->i2c) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!datetime_is_valid(dt)) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint8_t data[] = {
        DS1307_REG_SECONDS,
        dec_to_bcd(dt->seconds),  // CH = 0: генератор увімкнений
        dec_to_bcd(dt->minutes),
        dec_to_bcd(dt->hours),    // Біт 6 = 0: формат 24 години
        dt->day_of_week,
        dec_to_bcd(dt->day),
        dec_to_bcd(dt->month),
        dec_to_bcd((uint8_t)(dt->year - 2000U)),
    };

    return i2c_master_transmit(
        rtc->i2c,
        data,
        sizeof(data),
        DS1307_IO_TIMEOUT_MS
    );
}

esp_err_t ds1307_deinit(ds1307_t *rtc)
{
    if (!rtc) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!rtc->i2c) {
        return ESP_OK;
    }

    esp_err_t err = i2c_master_bus_rm_device(rtc->i2c);

    if (err == ESP_OK) {
        rtc->i2c = NULL;
    }

    return err;
}