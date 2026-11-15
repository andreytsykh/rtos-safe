#include "safe_storage.h"

#include <stddef.h>
#include <string.h>

#include "esp_log.h"

#define SAFE_STORAGE_MAGIC          0x53465331U
#define SAFE_STORAGE_VERSION        1U
#define SAFE_STORAGE_ADDRESS        0x0000U
#define SAFE_STORAGE_DEADLINE_VALID (1U << 0U)

typedef struct {
    uint32_t magic;
    uint64_t alarm_deadline_seconds;
    uint8_t attempts_used;
    uint8_t alarm_active;
    uint8_t flags;
    uint8_t version;
    uint8_t pin[SAFE_STORAGE_PIN_LENGTH];
    uint8_t pin_valid;
} safe_storage_record_t;

static const char *TAG = "safe_storage";
static const char *TEST_TAG = "SAFE_TEST";
static safe_storage_config_t storage_config;
static safe_storage_record_t record;
static bool initialized;

typedef struct {
    uint64_t rtc_now_seconds;
    bool rtc_now_valid;
} safe_storage_restore_info_t;

static bool is_leap_year(uint16_t year)
{
    return year % 4U == 0U &&
           (year % 100U != 0U || year % 400U == 0U);
}

static uint8_t days_in_month(uint16_t year, uint8_t month)
{
    static const uint8_t days[] = {
        31U, 28U, 31U, 30U, 31U, 30U,
        31U, 31U, 30U, 31U, 30U, 31U,
    };
    return month == 2U && is_leap_year(year)
        ? 29U
        : days[month - 1U];
}

static uint64_t datetime_to_seconds(const ds1307_datetime_t *datetime)
{
    uint64_t days = 0U;

    for (uint16_t year = 2000U; year < datetime->year; ++year) {
        days += is_leap_year(year) ? 366U : 365U;
    }
    for (uint8_t month = 1U; month < datetime->month; ++month) {
        days += days_in_month(datetime->year, month);
    }

    days += (uint64_t)datetime->day - 1U;
    return days * 86400U +
           (uint64_t)datetime->hours * 3600U +
           (uint64_t)datetime->minutes * 60U +
           datetime->seconds;
}

static esp_err_t read_rtc_seconds(uint64_t *seconds)
{
    if (storage_config.rtc == NULL) {
        return ESP_ERR_NOT_FOUND;
    }

    ds1307_datetime_t datetime;
    esp_err_t error = ds1307_read_datetime(storage_config.rtc, &datetime);
    if (error == ESP_OK) {
        *seconds = datetime_to_seconds(&datetime);
    }
    return error;
}

static esp_err_t write_record(void)
{
    return fram_write(
        storage_config.fram,
        SAFE_STORAGE_ADDRESS,
        &record,
        sizeof(record)
    );
}

static bool record_is_usable(void)
{
    return record.magic == SAFE_STORAGE_MAGIC &&
           record.version == SAFE_STORAGE_VERSION &&
           record.attempts_used <= storage_config.max_attempts &&
           record.alarm_active <= 1U &&
           record.pin_valid <= 1U;
}

static esp_err_t initialize_blank_record(void)
{
    record = (safe_storage_record_t) {
        .magic = SAFE_STORAGE_MAGIC,
        .version = SAFE_STORAGE_VERSION,
    };
    return write_record();
}

static void log_hms(const char *name, uint64_t seconds)
{
    uint32_t seconds_of_day = (uint32_t)(seconds % 86400U);
    uint32_t hours = seconds_of_day / 3600U;
    uint32_t minutes = (seconds_of_day % 3600U) / 60U;
    uint32_t secs = seconds_of_day % 60U;

    ESP_LOGI(
        TEST_TAG,
        "%s=%02lu:%02lu:%02lu",
        name,
        (unsigned long)hours,
        (unsigned long)minutes,
        (unsigned long)secs
    );
}

static void log_restored_state(
    const safe_storage_state_t *state,
    const safe_storage_restore_info_t *restore_info
)
{
    ESP_LOGI(TEST_TAG, "SNAPSHOT_BEGIN");
    ESP_LOGI(TEST_TAG, "RESTORED_ATTEMPTS=%u", state->attempts_used);
    ESP_LOGI(
        TEST_TAG,
        "RESTORED_STATE=%s",
        state->alarm_remaining_ms > 0U ? "ALARM" : "LOCKED"
    );

    if (restore_info->rtc_now_valid) {
        log_hms("RTC_NOW", restore_info->rtc_now_seconds);
    } else {
        ESP_LOGI(TEST_TAG, "RTC_NOW=UNAVAILABLE");
    }

    if ((record.flags & SAFE_STORAGE_DEADLINE_VALID) != 0U) {
        log_hms("ALARM_DEADLINE", record.alarm_deadline_seconds);
    } else {
        ESP_LOGI(TEST_TAG, "ALARM_DEADLINE=NONE");
    }

    ESP_LOGI(
        TEST_TAG,
        "REMAINING_SEC=%lu",
        (unsigned long)(state->alarm_remaining_ms / 1000U)
    );
    ESP_LOGI(TEST_TAG, "SNAPSHOT_END");
}

static esp_err_t restore_alarm(
    safe_storage_state_t *state,
    safe_storage_restore_info_t *restore_info
)
{
    if (record.alarm_active == 0U) {
        return ESP_OK;
    }

    state->attempts_used = storage_config.max_attempts;
    if ((record.flags & SAFE_STORAGE_DEADLINE_VALID) == 0U) {
        state->alarm_remaining_ms = storage_config.alarm_duration_ms;
        return ESP_OK;
    }

    uint64_t now;
    if (read_rtc_seconds(&now) != ESP_OK) {
        state->alarm_remaining_ms = storage_config.alarm_duration_ms;
        return ESP_OK;
    }
    restore_info->rtc_now_seconds = now;
    restore_info->rtc_now_valid = true;

    if (now >= record.alarm_deadline_seconds) {
        ESP_LOGI(TAG, "Stored alarm has expired");
        state->attempts_used = 0U;
        state->alarm_remaining_ms = 0U;
        record.attempts_used = 0U;
        record.alarm_active = 0U;
        record.flags = 0U;
        record.alarm_deadline_seconds = 0U;
        return write_record();
    }

    uint64_t remaining_ms =
        (record.alarm_deadline_seconds - now) * 1000U;
    state->alarm_remaining_ms = remaining_ms > UINT32_MAX
                              ? UINT32_MAX
                              : (uint32_t)remaining_ms;
    return ESP_OK;
}

esp_err_t safe_storage_init(
    const safe_storage_config_t *config,
    safe_storage_state_t *state
)
{
    if (config == NULL || config->fram == NULL || state == NULL ||
        config->max_attempts == 0U || config->alarm_duration_ms == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    if (initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    storage_config = *config;
    *state = (safe_storage_state_t) {0};

    esp_err_t error = fram_read(
        storage_config.fram,
        SAFE_STORAGE_ADDRESS,
        &record,
        sizeof(record)
    );
    if (error != ESP_OK) {
        return error;
    }
    if (!record_is_usable()) {
        ESP_LOGI(TAG, "Initializing blank FRAM state");
        error = initialize_blank_record();
        if (error != ESP_OK) {
            return error;
        }
    }

    initialized = true;
    state->attempts_used = record.attempts_used;
    state->pin_valid = record.pin_valid != 0U;
    if (state->pin_valid) {
        memcpy(state->pin, record.pin, sizeof(state->pin));
    }

    safe_storage_restore_info_t restore_info = {0};
    error = restore_alarm(state, &restore_info);
    if (error != ESP_OK) {
        return error;
    }

    if (!restore_info.rtc_now_valid) {
        uint64_t now;
        if (read_rtc_seconds(&now) == ESP_OK) {
            restore_info.rtc_now_seconds = now;
            restore_info.rtc_now_valid = true;
        }
    }

    log_restored_state(state, &restore_info);
    return ESP_OK;
}

esp_err_t safe_storage_save(uint8_t attempts_used, bool alarm_active)
{
    if (!initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    record.attempts_used = attempts_used;
    record.alarm_active = alarm_active ? 1U : 0U;
    record.flags = 0U;
    record.alarm_deadline_seconds = 0U;

    if (alarm_active) {
        uint64_t now;
        if (read_rtc_seconds(&now) == ESP_OK) {
            record.alarm_deadline_seconds =
                now + storage_config.alarm_duration_ms / 1000U;
            record.flags = SAFE_STORAGE_DEADLINE_VALID;
        }
    }

    return write_record();
}

esp_err_t safe_storage_save_pin(
    const uint8_t pin[SAFE_STORAGE_PIN_LENGTH]
)
{
    if (!initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (pin == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(record.pin, pin, sizeof(record.pin));
    record.pin_valid = 1U;
    return write_record();
}
