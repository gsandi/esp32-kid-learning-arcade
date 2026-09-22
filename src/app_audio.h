// app_audio.h — the one audio primitive the apps share: a tone sequence on the I2S amp.
// Implemented in main.cpp (melody_task). A sequence ends with {0, 0}. Fire and forget:
// each call spawns a short task; overlapping calls simply overlap.
#pragma once
#include <stdint.h>

struct Note { uint32_t freq; uint32_t ms; };

void audio_play(const Note* seq);

// Stream a 16 kHz mono 16-bit PCM WAV through the panel speaker. The file is
// read in fixed-size chunks so a five-minute note never needs to fit in RAM.
bool audio_play_wav(const char* path);
void audio_stop_wav(void);
bool audio_wav_playing(void);
