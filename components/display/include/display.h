#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DISPLAY_ROW_COUNT        8U
#define DISPLAY_MAX_TEXT_COLUMNS 16U

typedef enum {
    DISPLAY_TEXT_SCALE_1X = 1,
    DISPLAY_TEXT_SCALE_2X = 2,
    DISPLAY_TEXT_SCALE_3X = 3,
} display_text_scale_t;

typedef enum {
    DISPLAY_TEXT_ALIGN_LEFT = 0,
    DISPLAY_TEXT_ALIGN_CENTER,
    DISPLAY_TEXT_ALIGN_RIGHT,
} display_text_align_t;

typedef struct {
    display_text_scale_t scale;
    display_text_align_t alignment;
    bool inverted;
} display_text_style_t;

#define DISPLAY_TEXT_STYLE_DEFAULT()        \
    {                                       \
        .scale = DISPLAY_TEXT_SCALE_1X,      \
        .alignment = DISPLAY_TEXT_ALIGN_LEFT, \
        .inverted = false,                  \
    }

/* The application has one OLED, so the component is a singleton. */
esp_err_t display_init(i2c_master_bus_handle_t bus);

esp_err_t display_clear(void);

/*
 * Writes text starting at an SSD1306 page (row). Text is clipped and padded
 * to the width available for the selected scale.
 */
esp_err_t display_write_text(
    uint8_t row,
    const char *text,
    const display_text_style_t *style
);

#ifdef __cplusplus
}
#endif
