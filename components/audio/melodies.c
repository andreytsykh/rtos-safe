#include "audio_melodies.h"

#define NOTE_REST 0U
#define NOTE_C5   523U
#define NOTE_D5   587U
#define NOTE_E5   659U
#define NOTE_F5   698U
#define NOTE_G5   784U
#define NOTE_A5   880U

#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))

static const audio_note_t tick_notes[] = {
    { 2600U, 20U },
    { NOTE_REST, 20U },
};

static const audio_note_t unlocked_notes[] = {
    { NOTE_C5, 140U }, { NOTE_E5, 140U }, { NOTE_G5, 140U }, { NOTE_C5, 180U },
    { NOTE_REST, 60U },
    { NOTE_E5, 120U }, { NOTE_G5, 120U }, { NOTE_A5, 160U },
    { NOTE_REST, 50U },
    { NOTE_G5, 120U }, { NOTE_E5, 120U }, { NOTE_C5, 140U },
    { NOTE_REST, 60U },
    { NOTE_D5, 120U }, { NOTE_F5, 120U }, { NOTE_A5, 180U },
    { NOTE_REST, 60U },
    { NOTE_G5, 120U }, { NOTE_A5, 120U }, { NOTE_C5, 220U },
    { NOTE_REST, 80U },
    { NOTE_C5, 100U }, { NOTE_E5, 100U }, { NOTE_G5, 100U }, { NOTE_A5, 260U },
};

static const audio_note_t alarm_notes[] = {
    { 2400U, 70U }, { NOTE_REST, 25U },
    { 900U, 70U },  { NOTE_REST, 25U },
    { 2400U, 70U }, { NOTE_REST, 25U },
    { 900U, 70U },  { NOTE_REST, 80U },

    { 2800U, 55U }, { NOTE_REST, 20U },
    { 1100U, 55U }, { NOTE_REST, 20U },
    { 2800U, 55U }, { NOTE_REST, 20U },
    { 1100U, 55U }, { NOTE_REST, 80U },

    { 3200U, 45U }, { NOTE_REST, 15U },
    { 1300U, 45U }, { NOTE_REST, 15U },
    { 3200U, 45U }, { NOTE_REST, 15U },
    { 1300U, 45U }, { NOTE_REST, 100U },

    { 3000U, 40U }, { 2600U, 40U }, { 2200U, 40U }, { 1800U, 40U },
    { 1400U, 40U }, { 1000U, 40U }, { 700U, 120U },
    { NOTE_REST, 120U },
    { 3300U, 35U }, { NOTE_REST, 15U },
    { 3300U, 35U }, { NOTE_REST, 15U },
    { 3300U, 35U }, { NOTE_REST, 15U },
    { 900U, 180U },
    { NOTE_REST, 180U },
};

static const audio_melody_t melodies[] = {
    [AUDIO_SIGNAL_TICK] = {
        .notes = tick_notes,
        .note_count = ARRAY_SIZE(tick_notes),
        .loop = false,
    },
    [AUDIO_SIGNAL_UNLOCKED] = {
        .notes = unlocked_notes,
        .note_count = ARRAY_SIZE(unlocked_notes),
        .loop = false,
    },
    [AUDIO_SIGNAL_ALARM] = {
        .notes = alarm_notes,
        .note_count = ARRAY_SIZE(alarm_notes),
        .loop = true,
    },
};

const audio_melody_t *audio_melody_get(audio_signal_t signal)
{
    if ((signal < AUDIO_SIGNAL_TICK) ||
        (signal > AUDIO_SIGNAL_ALARM)) {
        return NULL;
    }

    return &melodies[signal];
}
