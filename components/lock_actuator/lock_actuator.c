#include "lock_actuator.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define LOCK_ACTUATOR_PWM_FREQUENCY_HZ 50U
#define LOCK_ACTUATOR_PWM_PERIOD_US    20000U
#define LOCK_ACTUATOR_DUTY_BITS        13U
#define LOCK_ACTUATOR_TASK_STACK_SIZE  3072U
#define LOCK_ACTUATOR_TASK_PRIORITY    4U

#define LOCK_ACTUATOR_LEDC_MODE    LEDC_LOW_SPEED_MODE
#define LOCK_ACTUATOR_LEDC_TIMER   LEDC_TIMER_1
#define LOCK_ACTUATOR_LEDC_CHANNEL LEDC_CHANNEL_1
#define LOCK_ACTUATOR_DUTY_MAX     ((1U << LOCK_ACTUATOR_DUTY_BITS) - 1U)

static const char *TAG = "lock_actuator";

static lock_actuator_config_t actuator_config;
static QueueHandle_t command_queue;
static TaskHandle_t task_handle;

static uint16_t clamp_pulse(uint16_t pulse_us);

static uint32_t pulse_width_to_duty(uint16_t pulse_width_us)
{
    pulse_width_us = clamp_pulse(pulse_width_us);
    return ((uint32_t)pulse_width_us * LOCK_ACTUATOR_DUTY_MAX) /
           LOCK_ACTUATOR_PWM_PERIOD_US;
}

static esp_err_t set_relay_enabled(bool enabled)
{
    int active_level = actuator_config.relay_active_low ? 0 : 1;
    int level = enabled ? active_level : !active_level;
    return gpio_set_level(actuator_config.relay_gpio, level);
}

static esp_err_t start_servo_pwm(uint16_t pulse_width_us)
{
    uint32_t duty = pulse_width_to_duty(pulse_width_us);
    ESP_RETURN_ON_ERROR(
        ledc_set_duty(
            LOCK_ACTUATOR_LEDC_MODE,
            LOCK_ACTUATOR_LEDC_CHANNEL,
            duty
        ),
        TAG,
        "set servo PWM duty"
    );

    return ledc_update_duty(
        LOCK_ACTUATOR_LEDC_MODE,
        LOCK_ACTUATOR_LEDC_CHANNEL
    );
}

static void stop_servo_pwm(void)
{
    esp_err_t error = ledc_stop(
        LOCK_ACTUATOR_LEDC_MODE,
        LOCK_ACTUATOR_LEDC_CHANNEL,
        0U
    );
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop servo PWM: %s", esp_err_to_name(error));
    }
}

static esp_err_t move_servo(lock_actuator_position_t position)
{
    uint16_t pulse_width_us =
        (position == LOCK_ACTUATOR_POSITION_OPEN)
            ? actuator_config.open_pulse_us
            : actuator_config.locked_pulse_us;

    ESP_RETURN_ON_ERROR(set_relay_enabled(true), TAG, "enable servo power");
    vTaskDelay(pdMS_TO_TICKS(actuator_config.power_stabilize_ms));

    esp_err_t error = start_servo_pwm(pulse_width_us);
    if (error == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(actuator_config.movement_time_ms));
    }

    stop_servo_pwm();
    esp_err_t relay_error = set_relay_enabled(false);

    if (error != ESP_OK) {
        return error;
    }
    return relay_error;
}

static void actuator_task(void *arg)
{
    (void)arg;

    bool position_known = false;
    lock_actuator_position_t current_position =
        LOCK_ACTUATOR_POSITION_LOCKED;

    ESP_LOGI(TAG, "Actuator task started");

    while (true) {
        lock_actuator_position_t requested_position;
        if (xQueueReceive(
                command_queue,
                &requested_position,
                portMAX_DELAY
            ) != pdTRUE) {
            continue;
        }

        if (position_known && requested_position == current_position) {
            continue;
        }

        ESP_LOGI(
            TAG,
            "Moving lock to %s",
            requested_position == LOCK_ACTUATOR_POSITION_OPEN
                ? "OPEN"
                : "LOCKED"
        );

        esp_err_t error = move_servo(requested_position);
        if (error != ESP_OK) {
            ESP_LOGE(TAG, "Lock movement failed: %s", esp_err_to_name(error));
            continue;
        }

        current_position = requested_position;
        position_known = true;
        ESP_LOGI(TAG, "Servo power disabled");
    }
}

static bool config_is_valid(const lock_actuator_config_t *config)
{
    return config != NULL &&
           GPIO_IS_VALID_OUTPUT_GPIO(config->relay_gpio) &&
           GPIO_IS_VALID_OUTPUT_GPIO(config->servo_pwm_gpio) &&
           config->relay_gpio != config->servo_pwm_gpio &&
           config->locked_pulse_us >= 500U &&
           config->locked_pulse_us <= 2500U &&
           config->open_pulse_us >= 500U &&
           config->open_pulse_us <= 2500U &&
           config->power_stabilize_ms > 0U &&
           config->movement_time_ms > 0U;
}

static uint16_t clamp_pulse(uint16_t pulse_us)
{
    if (pulse_us < 500U) {
        return 500U;
    }

    if (pulse_us > 2500U) {
        return 2500U;
    }

    return pulse_us;
}

esp_err_t lock_actuator_init(const lock_actuator_config_t *config)
{
    if (!config_is_valid(config)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (command_queue != NULL || task_handle != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    actuator_config = *config;

    const gpio_config_t relay_gpio_config = {
        .pin_bit_mask = 1ULL << config->relay_gpio,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(
        gpio_config(&relay_gpio_config),
        TAG,
        "configure relay GPIO"
    );
    ESP_RETURN_ON_ERROR(
        set_relay_enabled(false),
        TAG,
        "set relay inactive"
    );

    const ledc_timer_config_t timer_config = {
        .speed_mode = LOCK_ACTUATOR_LEDC_MODE,
        .duty_resolution = LEDC_TIMER_13_BIT,
        .timer_num = LOCK_ACTUATOR_LEDC_TIMER,
        .freq_hz = LOCK_ACTUATOR_PWM_FREQUENCY_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(
        ledc_timer_config(&timer_config),
        TAG,
        "configure servo PWM timer"
    );

    const ledc_channel_config_t channel_config = {
        .gpio_num = config->servo_pwm_gpio,
        .speed_mode = LOCK_ACTUATOR_LEDC_MODE,
        .channel = LOCK_ACTUATOR_LEDC_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LOCK_ACTUATOR_LEDC_TIMER,
        .duty = 0U,
        .hpoint = 0U,
    };
    ESP_RETURN_ON_ERROR(
        ledc_channel_config(&channel_config),
        TAG,
        "configure servo PWM channel"
    );

    command_queue = xQueueCreate(1U, sizeof(lock_actuator_position_t));
    if (command_queue == NULL) {
        stop_servo_pwm();
        return ESP_ERR_NO_MEM;
    }

    BaseType_t result = xTaskCreate(
        actuator_task,
        "lock_actuator",
        LOCK_ACTUATOR_TASK_STACK_SIZE,
        NULL,
        LOCK_ACTUATOR_TASK_PRIORITY,
        &task_handle
    );
    if (result != pdPASS) {
        vQueueDelete(command_queue);
        command_queue = NULL;
        stop_servo_pwm();
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(
        TAG,
        "Initialized: relay GPIO %d, servo PWM GPIO %d",
        config->relay_gpio,
        config->servo_pwm_gpio
    );
    return ESP_OK;
}

esp_err_t lock_actuator_set_position(lock_actuator_position_t position)
{
    if (command_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (position != LOCK_ACTUATOR_POSITION_LOCKED &&
        position != LOCK_ACTUATOR_POSITION_OPEN) {
        return ESP_ERR_INVALID_ARG;
    }

    return xQueueOverwrite(command_queue, &position) == pdPASS
               ? ESP_OK
               : ESP_FAIL;
}
