#include "safe_audit.h"

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_partition.h"

#define SAFE_AUDIT_RECORD_MAGIC 0x53414631U
#define SAFE_AUDIT_TIME_VALID   (1U << 0U)

typedef struct {
    uint32_t magic;
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hours;
    uint8_t minutes;
    uint8_t seconds;
    uint8_t result;
    uint8_t attempt_number;
    uint8_t attempts_remaining;
    uint8_t flags;
} safe_audit_record_t;

static const char *TAG = "safe_audit";
static QueueHandle_t attempt_queue;
static TaskHandle_t task_handle;
static safe_audit_config_t audit_config;
static const esp_partition_t *audit_partition;

static esp_err_t find_write_offset(size_t *offset)
{
    safe_audit_record_t record;

    for (*offset = 0U;
         *offset + sizeof(record) <= audit_partition->size;
         *offset += sizeof(record)) {
        esp_err_t error = esp_partition_read(
            audit_partition,
            *offset,
            &record,
            sizeof(record)
        );
        if (error != ESP_OK) {
            return error;
        }
        if (record.magic == UINT32_MAX) {
            return ESP_OK;
        }
        if (record.magic != SAFE_AUDIT_RECORD_MAGIC) {
            break;
        }
    }

    ESP_LOGI(TAG, "Audit partition full or unknown; erasing it");
    *offset = 0U;
    return esp_partition_erase_range(
        audit_partition,
        0U,
        audit_partition->size
    );
}

static void add_time(safe_audit_record_t *record)
{
    if (audit_config.rtc == NULL) {
        return;
    }

    ds1307_datetime_t datetime;
    if (ds1307_read_datetime(audit_config.rtc, &datetime) != ESP_OK) {
        return;
    }

    record->year = datetime.year;
    record->month = datetime.month;
    record->day = datetime.day;
    record->hours = datetime.hours;
    record->minutes = datetime.minutes;
    record->seconds = datetime.seconds;
    record->flags = SAFE_AUDIT_TIME_VALID;
}

static esp_err_t append_record(
    const safe_access_attempt_t *attempt,
    size_t *offset
)
{
    if (*offset + sizeof(safe_audit_record_t) > audit_partition->size) {
        esp_err_t error = esp_partition_erase_range(
            audit_partition,
            0U,
            audit_partition->size
        );
        if (error != ESP_OK) {
            return error;
        }
        *offset = 0U;
    }

    safe_audit_record_t record = {
        .magic = SAFE_AUDIT_RECORD_MAGIC,
        .result = (uint8_t)attempt->result,
        .attempt_number = attempt->attempt_number,
        .attempts_remaining = attempt->attempts_remaining,
    };
    add_time(&record);

    esp_err_t error = esp_partition_write(
        audit_partition,
        *offset,
        &record,
        sizeof(record)
    );
    if (error == ESP_OK) {
        *offset += sizeof(record);
    }
    return error;
}

static void log_attempt(const safe_access_attempt_t *attempt)
{
    ESP_LOGI(
        TAG,
        "Access event stored: result=%u attempt=%u remaining=%u",
        attempt->result,
        attempt->attempt_number,
        attempt->attempts_remaining
    );
}

static void safe_audit_task(void *arg)
{
    (void)arg;
    size_t write_offset;
    esp_err_t error = find_write_offset(&write_offset);

    while (true) {
        safe_access_attempt_t attempt;
        if (xQueueReceive(attempt_queue, &attempt, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (error == ESP_OK) {
            error = append_record(&attempt, &write_offset);
        }
        if (error == ESP_OK) {
            log_attempt(&attempt);
        } else {
            ESP_LOGE(TAG, "Audit write failed: %s", esp_err_to_name(error));
        }
    }
}

esp_err_t safe_audit_init(const safe_audit_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (task_handle != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    audit_partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA,
        (esp_partition_subtype_t)APP_AUDIT_PARTITION_SUBTYPE,
        APP_AUDIT_PARTITION_LABEL
    );
    if (audit_partition == NULL) {
        return ESP_ERR_NOT_FOUND;
    }

    attempt_queue = xQueueCreate(
        APP_AUDIT_QUEUE_LENGTH,
        sizeof(safe_access_attempt_t)
    );
    if (attempt_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    audit_config = *config;
    BaseType_t result = xTaskCreate(
        safe_audit_task,
        "safe_audit",
        APP_AUDIT_STACK_SIZE,
        NULL,
        APP_AUDIT_TASK_PRIORITY,
        &task_handle
    );
    return result == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

void safe_audit_log_attempt(const safe_access_attempt_t *attempt)
{
    if (attempt == NULL || attempt_queue == NULL) {
        return;
    }
    if (xQueueSend(attempt_queue, attempt, 0U) != pdTRUE) {
        ESP_LOGW(TAG, "Audit queue full; event dropped");
    }
}
