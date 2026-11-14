#include "button.h"

#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

typedef enum {
    BUTTON_STATE_IDLE = 0,
    BUTTON_STATE_PRESSING,
    BUTTON_STATE_LONG_PRESS,
} button_state_t;

static button_config_t button_config;
static button_state_t button_state;
static TimerHandle_t debounce_timer;
static TimerHandle_t long_press_timer;
static bool initialized;

static TickType_t milliseconds_to_ticks(uint32_t milliseconds)
{
    TickType_t ticks = pdMS_TO_TICKS(milliseconds);
    return ticks > 0U ? ticks : 1U;
}

static bool is_pressed(void)
{
    const int active_level = button_config.active_high ? 1 : 0;
    return gpio_get_level(button_config.gpio) == active_level;
}

static void emit_event(button_event_t event)
{
    if (button_config.on_event != NULL) {
        button_config.on_event(event, button_config.user_data);
    }
}

static void debounce_callback(TimerHandle_t timer)
{
    (void)timer;

    if (is_pressed() && button_state == BUTTON_STATE_IDLE) {
        button_state = BUTTON_STATE_PRESSING;
        (void)xTimerStart(long_press_timer, 0U);
    } else if (!is_pressed() && button_state == BUTTON_STATE_PRESSING) {
        (void)xTimerStop(long_press_timer, 0U);
        button_state = BUTTON_STATE_IDLE;
        emit_event(BUTTON_EVENT_SHORT_PRESS);
    } else if (!is_pressed() && button_state == BUTTON_STATE_LONG_PRESS) {
        button_state = BUTTON_STATE_IDLE;
    }
}

static void long_press_callback(TimerHandle_t timer)
{
    (void)timer;

    if (is_pressed() && button_state == BUTTON_STATE_PRESSING) {
        button_state = BUTTON_STATE_LONG_PRESS;
        emit_event(BUTTON_EVENT_LONG_PRESS);
    }
}

static void IRAM_ATTR gpio_isr(void *arg)
{
    (void)arg;
    BaseType_t task_woken = pdFALSE;
    (void)xTimerResetFromISR(debounce_timer, &task_woken);

    if (task_woken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

esp_err_t button_init(const button_config_t *config)
{
    if (config == NULL || !GPIO_IS_VALID_GPIO(config->gpio) ||
        config->debounce_ms == 0U || config->long_press_ms == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    if (initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    button_config = *config;
    const gpio_config_t gpio_configuration = {
        .pin_bit_mask = 1ULL << config->gpio,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = config->active_high ? GPIO_PULLUP_DISABLE
                                          : GPIO_PULLUP_ENABLE,
        .pull_down_en = config->active_high ? GPIO_PULLDOWN_ENABLE
                                            : GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    esp_err_t error = gpio_config(&gpio_configuration);
    if (error != ESP_OK) {
        return error;
    }

    debounce_timer = xTimerCreate(
        "button_debounce",
        milliseconds_to_ticks(config->debounce_ms),
        pdFALSE,
        NULL,
        debounce_callback
    );
    long_press_timer = xTimerCreate(
        "button_long",
        milliseconds_to_ticks(config->long_press_ms),
        pdFALSE,
        NULL,
        long_press_callback
    );
    if (debounce_timer == NULL || long_press_timer == NULL) {
        return ESP_ERR_NO_MEM;
    }

    error = gpio_install_isr_service(0);
    if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) {
        return error;
    }
    error = gpio_isr_handler_add(config->gpio, gpio_isr, NULL);
    if (error != ESP_OK) {
        return error;
    }

    /* Ignore the startup level. A real edge starts press processing. */
    button_state = BUTTON_STATE_IDLE;
    initialized = true;
    return ESP_OK;
}
