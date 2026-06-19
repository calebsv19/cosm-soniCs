#pragma once

#include <stdint.h>

int daw_test_wav_write_silence(const char* path, int sample_rate, uint32_t frames);
void daw_test_wav_write_silence_or_fail(const char* path,
                                        int sample_rate,
                                        uint32_t frames,
                                        const char* test_name);
