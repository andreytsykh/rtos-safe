#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "audio.h"

typedef struct {
    uint32_t frequency_hz;
    uint16_t duration_ms;
} audio_note_t;

typedef struct {
    const audio_note_t *notes;
    size_t note_count;
    bool loop;
} audio_melody_t;

const audio_melody_t *audio_melody_get(audio_signal_t signal);
