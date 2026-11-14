#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LOCK_ACTUATOR_POSITION_LOCKED = 0,
    LOCK_ACTUATOR_POSITION_OPEN,
} lock_actuator_position_t;

typedef struct {
    gpio_num_t relay_gpio;
    gpio_num_t servo_pwm_gpio;
    bool relay_active_low;
    uint16_t locked_pulse_us;
    uint16_t open_pulse_us;
    uint16_t power_stabilize_ms;
    uint16_t movement_time_ms;
} lock_actuator_config_t;

/* Initializes the relay in its inactive state and starts the actuator task. */
esp_err_t lock_actuator_init(const lock_actuator_config_t *config);

/* Queues the latest requested position; movement is performed asynchronously. */
esp_err_t lock_actuator_set_position(lock_actuator_position_t position);

#ifdef __cplusplus
}
#endif
