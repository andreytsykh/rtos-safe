#include "audio.h"

#include <stdbool.h>
#include <stddef.h>

#include "audio_melodies.h"
#include "driver/ledc.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define AUDIO_TIMER_MODE      LEDC_LOW_SPEED_MODE
#define AUDIO_TIMER_NUMBER    LEDC_TIMER_0
#define AUDIO_CHANNEL         LEDC_CHANNEL_0
#define AUDIO_DUTY_RESOLUTION LEDC_TIMER_10_BIT
#define AUDIO_DUTY            512U
#define AUDIO_TICK_MS         10U
#define AUDIO_MUTEX_TIMEOUT_MS 20U

static esp_timer_handle_t player_timer;
static SemaphoreHandle_t audio_mutex;
static const audio_melody_t *active_melody;
static size_t current_note;
static uint32_t elapsed_ms;
static uint32_t playback_elapsed_ms;
static uint32_t playback_limit_ms;
static bool initialized;
static bool playing;

static esp_err_t set_tone(uint32_t frequency_hz)
{
    uint32_t duty = AUDIO_DUTY;

    if (frequency_hz == 0U) {
        duty = 0U;
    } else {
        esp_err_t error = ledc_set_freq(
            AUDIO_TIMER_MODE,
            AUDIO_TIMER_NUMBER,
            frequency_hz
        );
        if (error != ESP_OK) {
            return error;
        }
    }

    esp_err_t error = ledc_set_duty(AUDIO_TIMER_MODE, AUDIO_CHANNEL, duty);
    if (error != ESP_OK) {
        return error;
    }

    return ledc_update_duty(AUDIO_TIMER_MODE, AUDIO_CHANNEL);
}

static void reset_playback(void)
{
    playing = false;
    active_melody = NULL;
    current_note = 0U;
    elapsed_ms = 0U;
    playback_elapsed_ms = 0U;
    playback_limit_ms = 0U;
}

/* Caller must hold audio_mutex. */
static void finish_playback_locked(void)
{
    (void)esp_timer_stop(player_timer);
    reset_playback();
    (void)set_tone(0U);
}

static void player_tick(void *arg)
{
    (void)arg;

    /* Do not block the shared esp_timer task if an audio command is running. */
    if (xSemaphoreTake(audio_mutex, 0U) != pdTRUE) {
        return;
    }

    if (!playing || (active_melody == NULL)) {
        xSemaphoreGive(audio_mutex);
        return;
    }

    elapsed_ms += AUDIO_TICK_MS;
    playback_elapsed_ms += AUDIO_TICK_MS;

    if (playback_limit_ms > 0U &&
        playback_elapsed_ms >= playback_limit_ms) {
        finish_playback_locked();
        xSemaphoreGive(audio_mutex);
        return;
    }

    if (elapsed_ms < active_melody->notes[current_note].duration_ms) {
        xSemaphoreGive(audio_mutex);
        return;
    }

    elapsed_ms = 0U;
    current_note++;

    if (current_note >= active_melody->note_count) {
        if (!active_melody->loop) {
            finish_playback_locked();
            xSemaphoreGive(audio_mutex);
            return;
        }

        current_note = 0U;
    }

    (void)set_tone(active_melody->notes[current_note].frequency_hz);
    xSemaphoreGive(audio_mutex);
}

esp_err_t audio_init(const audio_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    const ledc_timer_config_t timer_config = {
        .speed_mode = AUDIO_TIMER_MODE,
        .timer_num = AUDIO_TIMER_NUMBER,
        .duty_resolution = AUDIO_DUTY_RESOLUTION,
        .freq_hz = 1000U,
        .clk_cfg = LEDC_AUTO_CLK,
    };

    esp_err_t error = ledc_timer_config(&timer_config);
    if (error != ESP_OK) {
        return error;
    }

    const ledc_channel_config_t channel_config = {
        .gpio_num = config->gpio,
        .speed_mode = AUDIO_TIMER_MODE,
        .channel = AUDIO_CHANNEL,
        .timer_sel = AUDIO_TIMER_NUMBER,
        .duty = 0U,
        .hpoint = 0U,
    };

    error = ledc_channel_config(&channel_config);
    if (error != ESP_OK) {
        return error;
    }

    const esp_timer_create_args_t timer_args = {
        .callback = player_tick,
        .name = "audio_player",
    };

    error = esp_timer_create(&timer_args, &player_timer);
    if (error != ESP_OK) {
        return error;
    }

    audio_mutex = xSemaphoreCreateMutex();
    if (audio_mutex == NULL) {
        (void)esp_timer_delete(player_timer);
        player_timer = NULL;
        return ESP_ERR_NO_MEM;
    }

    initialized = true;
    reset_playback();
    return ESP_OK;
}

static esp_err_t start_playback(
    audio_signal_t signal,
    uint32_t duration_ms
)
{
    if (!initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    const audio_melody_t *melody = audio_melody_get(signal);
    if (melody == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(
            audio_mutex,
            pdMS_TO_TICKS(AUDIO_MUTEX_TIMEOUT_MS)
        ) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    (void)esp_timer_stop(player_timer);
    active_melody = melody;
    current_note = 0U;
    elapsed_ms = 0U;
    playback_elapsed_ms = 0U;
    playback_limit_ms = duration_ms;
    playing = true;

    esp_err_t error = set_tone(active_melody->notes[0U].frequency_hz);
    if (error == ESP_OK) {
        error = esp_timer_start_periodic(
            player_timer,
            AUDIO_TICK_MS * 1000ULL
        );
    }

    if (error != ESP_OK) {
        reset_playback();
        (void)set_tone(0U);
    }

    xSemaphoreGive(audio_mutex);
    return error;
}

esp_err_t audio_play(audio_signal_t signal)
{
    return start_playback(signal, 0U);
}

esp_err_t audio_play_for(audio_signal_t signal, uint32_t duration_ms)
{
    if (duration_ms == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    return start_playback(signal, duration_ms);
}

esp_err_t audio_stop(void)
{
    if (!initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(
            audio_mutex,
            pdMS_TO_TICKS(AUDIO_MUTEX_TIMEOUT_MS)
        ) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    (void)esp_timer_stop(player_timer);
    reset_playback();
    esp_err_t error = set_tone(0U);
    xSemaphoreGive(audio_mutex);
    return error;
}
