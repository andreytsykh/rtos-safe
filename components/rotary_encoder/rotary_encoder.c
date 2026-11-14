#include "rotary_encoder.h"

#include <stdbool.h>

#include "driver/pulse_cnt.h"
#include "esp_attr.h"
#include "freertos/queue.h"

#define PCNT_MAX_COUNTS_PER_DETENT 30000
#define ENCODER_EVENT_QUEUE_LENGTH 16U

typedef struct {
    pcnt_unit_handle_t unit;
    pcnt_channel_handle_t channel_a;
    pcnt_channel_handle_t channel_b;
    QueueHandle_t event_queue;
    int counts_per_detent;
    bool initialized;
} encoder_context_t;

static encoder_context_t encoder;

static bool IRAM_ATTR on_count_reached(
    pcnt_unit_handle_t unit,
    const pcnt_watch_event_data_t *event_data,
    void *user_data
)
{
    (void)unit;
    (void)user_data;

    const rotary_encoder_event_t event =
        event_data->watch_point_value > 0
            ? ROTARY_ENCODER_EVENT_CW
            : ROTARY_ENCODER_EVENT_CCW;
    BaseType_t task_woken = pdFALSE;
    (void)xQueueSendFromISR(encoder.event_queue, &event, &task_woken);
    return task_woken == pdTRUE;
}

esp_err_t rotary_encoder_init(const rotary_encoder_config_t *config)
{
    if (config == NULL || !GPIO_IS_VALID_GPIO(config->gpio_a) ||
        !GPIO_IS_VALID_GPIO(config->gpio_b) ||
        config->gpio_a == config->gpio_b ||
        config->counts_per_detent <= 0 ||
        config->counts_per_detent > PCNT_MAX_COUNTS_PER_DETENT) {
        return ESP_ERR_INVALID_ARG;
    }
    if (encoder.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    encoder.event_queue = xQueueCreate(
        ENCODER_EVENT_QUEUE_LENGTH,
        sizeof(rotary_encoder_event_t)
    );
    if (encoder.event_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }
    encoder.counts_per_detent = config->counts_per_detent;

    const pcnt_unit_config_t unit_config = {
        .high_limit = config->counts_per_detent,
        .low_limit = -config->counts_per_detent,
    };
    esp_err_t error = pcnt_new_unit(&unit_config, &encoder.unit);
    if (error != ESP_OK) {
        return error;
    }

    error = pcnt_unit_add_watch_point(encoder.unit, config->counts_per_detent);
    if (error == ESP_OK) {
        error = pcnt_unit_add_watch_point(
            encoder.unit,
            -config->counts_per_detent
        );
    }
    if (error == ESP_OK) {
        const pcnt_event_callbacks_t callbacks = {
            .on_reach = on_count_reached,
        };
        error = pcnt_unit_register_event_callbacks(
            encoder.unit,
            &callbacks,
            NULL
        );
    }
    if (error != ESP_OK) {
        return error;
    }

    const pcnt_chan_config_t channel_a_config = {
        .edge_gpio_num = config->gpio_a,
        .level_gpio_num = config->gpio_b,
    };
    error = pcnt_new_channel(
        encoder.unit,
        &channel_a_config,
        &encoder.channel_a
    );
    if (error != ESP_OK) {
        return error;
    }

    const pcnt_chan_config_t channel_b_config = {
        .edge_gpio_num = config->gpio_b,
        .level_gpio_num = config->gpio_a,
    };
    error = pcnt_new_channel(
        encoder.unit,
        &channel_b_config,
        &encoder.channel_b
    );
    if (error != ESP_OK) {
        return error;
    }

    error = pcnt_channel_set_edge_action(
        encoder.channel_a,
        PCNT_CHANNEL_EDGE_ACTION_DECREASE,
        PCNT_CHANNEL_EDGE_ACTION_INCREASE
    );
    if (error == ESP_OK) {
        error = pcnt_channel_set_level_action(
            encoder.channel_a,
            PCNT_CHANNEL_LEVEL_ACTION_KEEP,
            PCNT_CHANNEL_LEVEL_ACTION_INVERSE
        );
    }
    if (error == ESP_OK) {
        error = pcnt_channel_set_edge_action(
            encoder.channel_b,
            PCNT_CHANNEL_EDGE_ACTION_INCREASE,
            PCNT_CHANNEL_EDGE_ACTION_DECREASE
        );
    }
    if (error == ESP_OK) {
        error = pcnt_channel_set_level_action(
            encoder.channel_b,
            PCNT_CHANNEL_LEVEL_ACTION_KEEP,
            PCNT_CHANNEL_LEVEL_ACTION_INVERSE
        );
    }
    if (error != ESP_OK) {
        return error;
    }

    if (config->glitch_filter_ns > 0U) {
        const pcnt_glitch_filter_config_t filter = {
            .max_glitch_ns = config->glitch_filter_ns,
        };
        error = pcnt_unit_set_glitch_filter(encoder.unit, &filter);
        if (error != ESP_OK) {
            return error;
        }
    }

    error = pcnt_unit_enable(encoder.unit);
    if (error == ESP_OK) {
        error = pcnt_unit_clear_count(encoder.unit);
    }
    if (error == ESP_OK) {
        error = pcnt_unit_start(encoder.unit);
    }
    if (error == ESP_OK) {
        encoder.initialized = true;
    }
    return error;
}

esp_err_t rotary_encoder_wait_event(
    rotary_encoder_event_t *out_event,
    TickType_t timeout
)
{
    if (!encoder.initialized || out_event == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    return xQueueReceive(encoder.event_queue, out_event, timeout) == pdTRUE
        ? ESP_OK
        : ESP_ERR_TIMEOUT;
}

esp_err_t rotary_encoder_reset(void)
{
    if (!encoder.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t error = pcnt_unit_stop(encoder.unit);
    if (error == ESP_OK) {
        error = pcnt_unit_clear_count(encoder.unit);
    }
    if (error == ESP_OK) {
        (void)xQueueReset(encoder.event_queue);
        error = pcnt_unit_start(encoder.unit);
    }
    return error;
}
