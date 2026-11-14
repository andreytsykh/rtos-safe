#include "display.h"

#include <stddef.h>
#include <string.h>

#include "ssd1306.h"

#define DISPLAY_ALTERNATE_I2C_ADDRESS 0x3DU

static ssd1306_handle_t device;

static bool style_is_valid(const display_text_style_t *style)
{
    if (style == NULL) {
        return false;
    }

    bool scale_valid =
        (style->scale == DISPLAY_TEXT_SCALE_1X) ||
        (style->scale == DISPLAY_TEXT_SCALE_2X) ||
        (style->scale == DISPLAY_TEXT_SCALE_3X);
    bool alignment_valid =
        (style->alignment == DISPLAY_TEXT_ALIGN_LEFT) ||
        (style->alignment == DISPLAY_TEXT_ALIGN_CENTER) ||
        (style->alignment == DISPLAY_TEXT_ALIGN_RIGHT);

    return scale_valid && alignment_valid;
}

static size_t alignment_offset(
    display_text_align_t alignment,
    size_t capacity,
    size_t length
)
{
    switch (alignment) {
        case DISPLAY_TEXT_ALIGN_CENTER:
            return (capacity - length) / 2U;

        case DISPLAY_TEXT_ALIGN_RIGHT:
            return capacity - length;

        case DISPLAY_TEXT_ALIGN_LEFT:
        default:
            return 0U;
    }
}

esp_err_t display_init(i2c_master_bus_handle_t bus)
{
    if (bus == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (device != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    ssd1306_config_t config = I2C_SSD1306_128x64_CONFIG_DEFAULT;
    esp_err_t error = ssd1306_init(bus, &config, &device);
    if (error == ESP_ERR_NOT_FOUND) {
        if (device != NULL) {
            (void)ssd1306_delete(device);
            device = NULL;
        }

        config.i2c_address = DISPLAY_ALTERNATE_I2C_ADDRESS;
        error = ssd1306_init(bus, &config, &device);
    }
    if (error == ESP_OK) {
        error = ssd1306_clear_display(device, false);
    }

    if (error != ESP_OK) {
        if (device != NULL) {
            (void)ssd1306_delete(device);
            device = NULL;
        }
    }
    return error;
}

esp_err_t display_clear(void)
{
    if (device == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    return ssd1306_clear_display(device, false);
}

esp_err_t display_write_text(
    uint8_t row,
    const char *text,
    const display_text_style_t *style
)
{
    if (device == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (text == NULL || !style_is_valid(style)) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t scale = (uint8_t)style->scale;
    if ((row >= DISPLAY_ROW_COUNT) ||
        ((uint16_t)row + scale > DISPLAY_ROW_COUNT)) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t capacity = DISPLAY_MAX_TEXT_COLUMNS / scale;
    size_t length = strnlen(text, capacity);
    size_t offset = alignment_offset(
        style->alignment,
        capacity,
        length
    );

    char line[DISPLAY_MAX_TEXT_COLUMNS + 1U];
    memset(line, ' ', capacity);
    memcpy(&line[offset], text, length);
    line[capacity] = '\0';

    switch (style->scale) {
        case DISPLAY_TEXT_SCALE_1X:
            return ssd1306_display_text(
                device,
                row,
                line,
                style->inverted
            );

        case DISPLAY_TEXT_SCALE_2X:
            return ssd1306_display_text_x2(
                device,
                row,
                line,
                style->inverted
            );

        case DISPLAY_TEXT_SCALE_3X:
            return ssd1306_display_text_x3(
                device,
                row,
                line,
                style->inverted
            );

        default:
            return ESP_ERR_INVALID_ARG;
    }
}
