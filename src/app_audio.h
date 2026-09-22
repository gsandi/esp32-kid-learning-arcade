// app_audio.h — the one audio primitive the apps share: a tone sequence on the I2S amp.
// Implemented in main.cpp (melody_task). A sequence ends with {0, 0}. Fire and forget:
// each call spawns a short task; overlapping calls simply overlap.
#pragma once
#include <stdint.h>

struct Note { uint32_t freq; uint32_t ms; };

void audio_play(const Note* seq);
