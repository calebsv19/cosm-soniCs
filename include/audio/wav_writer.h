#pragma once

#include <stdbool.h>
#include <stdint.h>

bool wav_write_pcm16(const char* path, const float* data, uint64_t frames, int channels, int sample_rate);
