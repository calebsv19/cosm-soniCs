#include "audio/media_clip.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool read_u32_le(FILE* file, uint32_t* out) {
    unsigned char bytes[4];
    if (fread(bytes, 1, 4, file) != 4) {
        return false;
    }
    *out = (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
    return true;
}

static bool read_u16_le(FILE* file, uint16_t* out) {
    unsigned char bytes[2];
    if (fread(bytes, 1, 2, file) != 2) {
        return false;
    }
    *out = (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
    return true;
}

void audio_media_clip_free(AudioMediaClip* clip) {
    if (!clip) {
        return;
    }
    free(clip->samples);
    clip->samples = NULL;
    clip->frame_count = 0;
    clip->channels = 0;
    clip->sample_rate = 0;
}

static float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

bool audio_media_clip_load_wav(const char* path, int target_sample_rate, AudioMediaClip* out_clip) {
    if (!path || !out_clip) {
        return false;
    }

    FILE* file = fopen(path, "rb");
    if (!file) {
        return false;
    }

    char chunk_id[4];
    if (fread(chunk_id, 1, 4, file) != 4 || memcmp(chunk_id, "RIFF", 4) != 0) {
        fclose(file);
        return false;
    }

    uint32_t riff_size = 0;
    if (!read_u32_le(file, &riff_size)) {
        fclose(file);
        return false;
    }

    if (fread(chunk_id, 1, 4, file) != 4 || memcmp(chunk_id, "WAVE", 4) != 0) {
        fclose(file);
        return false;
    }

    uint16_t format_tag = 0;
    uint16_t channels = 0;
    uint32_t sample_rate = 0;
    uint16_t bits_per_sample = 0;
    uint32_t data_chunk_size = 0;
    long data_offset = 0;

    while (!feof(file)) {
        if (fread(chunk_id, 1, 4, file) != 4) {
            break;
        }
        uint32_t chunk_size = 0;
        if (!read_u32_le(file, &chunk_size)) {
            break;
        }

        if (memcmp(chunk_id, "fmt ", 4) == 0) {
            if (!read_u16_le(file, &format_tag)) {
                break;
            }
            if (!read_u16_le(file, &channels)) {
                break;
            }
            if (!read_u32_le(file, &sample_rate)) {
                break;
            }
            uint32_t byte_rate;
            uint16_t block_align;
            if (!read_u32_le(file, &byte_rate)) {
                break;
            }
            if (!read_u16_le(file, &block_align)) {
                break;
            }
            if (!read_u16_le(file, &bits_per_sample)) {
                break;
            }
            long remaining = (long)chunk_size - 16;
            if (remaining > 0) {
                fseek(file, remaining, SEEK_CUR);
            }
        } else if (memcmp(chunk_id, "data", 4) == 0) {
            data_chunk_size = chunk_size;
            data_offset = ftell(file);
            fseek(file, chunk_size, SEEK_CUR);
        } else {
            fseek(file, chunk_size, SEEK_CUR);
        }
    }

    if (format_tag != 1 || channels == 0 || sample_rate == 0 || bits_per_sample == 0 || data_chunk_size == 0) {
        fclose(file);
        return false;
    }

    if (fseek(file, data_offset, SEEK_SET) != 0) {
        fclose(file);
        return false;
    }

    uint32_t bytes_per_sample = bits_per_sample / 8;
    if (bytes_per_sample == 0) {
        fclose(file);
        return false;
    }

    uint64_t total_samples = data_chunk_size / bytes_per_sample;
    uint64_t frame_count = total_samples / channels;
    float* samples = (float*)malloc(sizeof(float) * (size_t)total_samples);
    if (!samples) {
        fclose(file);
        return false;
    }

    bool ok = true;
    for (uint64_t i = 0; i < frame_count && ok; ++i) {
        for (uint16_t ch = 0; ch < channels; ++ch) {
            float value = 0.0f;
            if (bytes_per_sample == 2) {
                int16_t sample16;
                if (fread(&sample16, sizeof(int16_t), 1, file) != 1) {
                    ok = false;
                    break;
                }
                value = (float)sample16 / 32768.0f;
            } else if (bytes_per_sample == 4 && bits_per_sample == 32) {
                float sample32;
                if (fread(&sample32, sizeof(float), 1, file) != 1) {
                    ok = false;
                    break;
                }
                value = sample32;
            } else {
                ok = false;
                break;
            }
            samples[i * channels + ch] = value;
        }
    }

    if (!ok) {
        free(samples);
        fclose(file);
        return false;
    }

    fclose(file);

    int desired_rate = target_sample_rate > 0 ? target_sample_rate : (int)sample_rate;
    if ((int)sample_rate == desired_rate) {
        out_clip->samples = samples;
        out_clip->frame_count = frame_count;
        out_clip->channels = (int)channels;
        out_clip->sample_rate = (int)sample_rate;
        return true;
    }

    double ratio = (double)desired_rate / (double)sample_rate;
    uint64_t new_frames = (uint64_t)((double)frame_count * ratio + 0.5);
    if (new_frames == 0) {
        new_frames = 1;
    }

    float* resampled = (float*)malloc((size_t)new_frames * (size_t)channels * sizeof(float));
    if (!resampled) {
        free(samples);
        return false;
    }

    double inv_ratio = (double)sample_rate / (double)desired_rate;
    for (uint64_t i = 0; i < new_frames; ++i) {
        double src_pos = (double)i * inv_ratio;
        uint64_t src_index = (uint64_t)src_pos;
        double frac = src_pos - (double)src_index;
        uint64_t src_index_next = src_index + 1;
        if (src_index_next >= frame_count) {
            src_index_next = frame_count - 1;
        }
        for (uint16_t ch = 0; ch < channels; ++ch) {
            float a = samples[src_index * channels + ch];
            float b = samples[src_index_next * channels + ch];
            resampled[i * channels + ch] = lerp(a, b, (float)frac);
        }
    }

    free(samples);

    out_clip->samples = resampled;
    out_clip->frame_count = new_frames;
    out_clip->channels = (int)channels;
    out_clip->sample_rate = desired_rate;
    return true;
}
