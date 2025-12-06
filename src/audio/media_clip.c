#include "audio/media_clip.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#if defined(__APPLE__)
#include <AudioToolbox/AudioToolbox.h>
#endif

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

static bool finalize_clip(float* samples,
                          uint64_t frame_count,
                          int channels,
                          int source_rate,
                          int target_sample_rate,
                          AudioMediaClip* out_clip) {
    if (!samples || !out_clip || channels <= 0 || frame_count == 0 || source_rate <= 0) {
        free(samples);
        return false;
    }

    int desired_rate = target_sample_rate > 0 ? target_sample_rate : source_rate;
    if (desired_rate <= 0) {
        desired_rate = source_rate;
    }

    if (source_rate == desired_rate) {
        out_clip->samples = samples;
        out_clip->frame_count = frame_count;
        out_clip->channels = channels;
        out_clip->sample_rate = source_rate;
        return true;
    }

    double ratio = (double)desired_rate / (double)source_rate;
    uint64_t new_frames = (uint64_t)((double)frame_count * ratio + 0.5);
    if (new_frames == 0) {
        new_frames = 1;
    }

    float* resampled = (float*)malloc((size_t)new_frames * (size_t)channels * sizeof(float));
    if (!resampled) {
        free(samples);
        return false;
    }

    double inv_ratio = (double)source_rate / (double)desired_rate;
    for (uint64_t i = 0; i < new_frames; ++i) {
        double src_pos = (double)i * inv_ratio;
        uint64_t src_index = (uint64_t)src_pos;
        if (src_index >= frame_count) {
            src_index = frame_count - 1;
        }
        double frac = src_pos - (double)src_index;
        uint64_t next_index = src_index + 1 < frame_count ? src_index + 1 : src_index;
        for (int ch = 0; ch < channels; ++ch) {
            float a = samples[src_index * (uint64_t)channels + (uint64_t)ch];
            float b = samples[next_index * (uint64_t)channels + (uint64_t)ch];
            resampled[i * (uint64_t)channels + (uint64_t)ch] = lerp(a, b, (float)frac);
        }
    }

    free(samples);
    out_clip->samples = resampled;
    out_clip->frame_count = new_frames;
    out_clip->channels = channels;
    out_clip->sample_rate = desired_rate;
    return true;
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

    bool pcm_int = (format_tag == 1);
    bool pcm_float = (format_tag == 3);
    if ((!pcm_int && !pcm_float) || channels == 0 || sample_rate == 0 || bits_per_sample == 0 || data_chunk_size == 0) {
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
            if (pcm_int && bytes_per_sample == 2) {
                int16_t sample16;
                if (fread(&sample16, sizeof(int16_t), 1, file) != 1) {
                    ok = false;
                    break;
                }
                value = (float)sample16 / 32768.0f;
            } else if (pcm_float && bytes_per_sample == 4 && bits_per_sample == 32) {
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

    return finalize_clip(samples, frame_count, (int)channels, (int)sample_rate, target_sample_rate, out_clip);
}

#if defined(__APPLE__)
static bool audio_media_clip_load_mp3(const char* path, int target_sample_rate, AudioMediaClip* out_clip) {
    if (!path || !out_clip) {
        return false;
    }

    CFURLRef url = CFURLCreateFromFileSystemRepresentation(NULL, (const UInt8*)path, (CFIndex)strlen(path), false);
    if (!url) {
        return false;
    }

    ExtAudioFileRef file = NULL;
    OSStatus status = ExtAudioFileOpenURL(url, &file);
    CFRelease(url);
    if (status != noErr || !file) {
        return false;
    }

    AudioStreamBasicDescription fileFormat = {0};
    UInt32 formatSize = (UInt32)sizeof(fileFormat);
    status = ExtAudioFileGetProperty(file, kExtAudioFileProperty_FileDataFormat, &formatSize, &fileFormat);
    if (status != noErr || fileFormat.mChannelsPerFrame <= 0) {
        ExtAudioFileDispose(file);
        return false;
    }

    SInt64 frameCount = 0;
    UInt32 frameCountSize = (UInt32)sizeof(frameCount);
    status = ExtAudioFileGetProperty(file, kExtAudioFileProperty_FileLengthFrames, &frameCountSize, &frameCount);
    if (status != noErr || frameCount < 0) {
        ExtAudioFileDispose(file);
        return false;
    }

    int channels = (int)fileFormat.mChannelsPerFrame;

    Float64 desiredRate = target_sample_rate > 0 ? (Float64)target_sample_rate : fileFormat.mSampleRate;
    if (desiredRate <= 0.0) {
        desiredRate = fileFormat.mSampleRate;
    }

    AudioStreamBasicDescription clientFormat = {0};
    clientFormat.mSampleRate = desiredRate;
    clientFormat.mFormatID = kAudioFormatLinearPCM;
    clientFormat.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked | kAudioFormatFlagsNativeEndian;
    clientFormat.mBitsPerChannel = 8 * sizeof(float);
    clientFormat.mChannelsPerFrame = (UInt32)channels;
    clientFormat.mFramesPerPacket = 1;
    clientFormat.mBytesPerFrame = (UInt32)(sizeof(float) * (size_t)channels);
    clientFormat.mBytesPerPacket = clientFormat.mBytesPerFrame;

    status = ExtAudioFileSetProperty(file, kExtAudioFileProperty_ClientDataFormat, (UInt32)sizeof(clientFormat), &clientFormat);
    if (status != noErr) {
        ExtAudioFileDispose(file);
        return false;
    }

    uint64_t total_frames = (frameCount > 0) ? (uint64_t)frameCount : 0;
    if (total_frames == 0) {
        total_frames = 1; /* ensure allocation for streaming read */
    }

    float* samples = (float*)malloc((size_t)total_frames * (size_t)channels * sizeof(float));
    if (!samples) {
        ExtAudioFileDispose(file);
        return false;
    }

    uint64_t frames_read_total = 0;
    while (true) {
        uint64_t frames_left = (frameCount > 0) ? (uint64_t)frameCount - frames_read_total : 4096;
        if (frameCount > 0 && frames_left == 0) {
            break;
        }
        UInt32 frames_to_read = (UInt32)((frames_left > 4096) ? 4096 : frames_left);
        AudioBufferList bufferList;
        bufferList.mNumberBuffers = 1;
        bufferList.mBuffers[0].mNumberChannels = (UInt32)channels;
        bufferList.mBuffers[0].mData = samples + frames_read_total * (uint64_t)channels;
        bufferList.mBuffers[0].mDataByteSize = frames_to_read * (UInt32)channels * (UInt32)sizeof(float);

        status = ExtAudioFileRead(file, &frames_to_read, &bufferList);
        if (status != noErr) {
            free(samples);
            ExtAudioFileDispose(file);
            return false;
        }
        if (frames_to_read == 0) {
            break;
        }
        frames_read_total += frames_to_read;
        if (frameCount <= 0) {
            /* extend buffer for streaming files with unknown length */
            float* resized = (float*)realloc(samples, (frames_read_total + 4096) * (size_t)channels * sizeof(float));
            if (!resized) {
                free(samples);
                ExtAudioFileDispose(file);
                return false;
            }
            samples = resized;
            total_frames = frames_read_total + 4096;
        }
    }

    ExtAudioFileDispose(file);

    size_t used_bytes = (size_t)frames_read_total * (size_t)channels * sizeof(float);
    float* trimmed = (float*)realloc(samples, used_bytes);
    if (trimmed) {
        samples = trimmed;
    }

    if (frames_read_total == 0) {
        free(samples);
        return false;
    }

    return finalize_clip(samples, frames_read_total, channels, (int)desiredRate, target_sample_rate, out_clip);
}
#else
static bool audio_media_clip_load_mp3(const char* path, int target_sample_rate, AudioMediaClip* out_clip) {
    (void)path;
    (void)target_sample_rate;
    (void)out_clip;
    return false;
}
#endif

bool audio_media_clip_load(const char* path, int target_sample_rate, AudioMediaClip* out_clip) {
    if (!path || !out_clip) {
        return false;
    }

    const char* dot = strrchr(path, '.');
    if (dot) {
        if (strcasecmp(dot, ".wav") == 0) {
            return audio_media_clip_load_wav(path, target_sample_rate, out_clip);
        }
        if (strcasecmp(dot, ".mp3") == 0) {
            return audio_media_clip_load_mp3(path, target_sample_rate, out_clip);
        }
    }

    if (audio_media_clip_load_wav(path, target_sample_rate, out_clip)) {
        return true;
    }
    return audio_media_clip_load_mp3(path, target_sample_rate, out_clip);
}
