#include "audio/media_clip.h"
#include "audio/resample.h"
#include <math.h>
#include <limits.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <spawn.h>
#include <sys/wait.h>
#include <errno.h>
#include <signal.h>
#include <sys/stat.h>
#include <time.h>
#if defined(__APPLE__)
#include <AudioToolbox/AudioToolbox.h>
#endif

extern char **environ;

// Scoped per-thread decode policy avoids sharing mutable cancellation state between callers.
static _Thread_local const AudioMediaLoadControl* load_control;
// Observes the owner cancellation flag between bounded decode/conversion chunks.
static bool load_cancelled(void* unused) {
    (void)unused;
    return load_control && load_control->cancelled && load_control->cancelled(load_control->user);
}
// Rejects overflowing or over-budget source/output sample buffers before allocation.
static bool load_size_allowed(uint64_t frames, int channels, int source_rate, int target_rate) {
    if (load_cancelled(NULL) || channels <= 0 || source_rate <= 0 || frames > SIZE_MAX / sizeof(float) / (size_t)channels) return false;
    long double output = floorl((long double)frames * (target_rate > 0 ? target_rate : source_rate) / source_rate + .5L);
    if (output < 1) output = 1;
    if (output > SIZE_MAX / sizeof(float) / (size_t)channels) return false;
    if (!load_control || !load_control->max_sample_bytes) return true;
    return frames * channels * sizeof(float) <= load_control->max_sample_bytes &&
           output * channels * sizeof(float) <= load_control->max_sample_bytes;
}

static bool run_mp3_ffmpeg_decode_to_wav(const char* input_path, int target_sample_rate, const char* output_path) {
    if (!input_path || !output_path || load_cancelled(NULL)) {
        return false;
    }

    const char* ffmpeg_candidates[] = {
        "/opt/homebrew/bin/ffmpeg",
        "/usr/local/bin/ffmpeg",
        "/usr/bin/ffmpeg",
        "ffmpeg"
    };

    (void)target_sample_rate; // Decode at source rate; the common offline converter owns resampling.

    const char* argv_common[] = {
        "-v", "error",
        "-nostdin",
        "-y",
        "-i", input_path,
        "-f", "wav",
        "-acodec", "pcm_f32le",
        output_path,
        NULL
    };

    for (size_t i = 0; i < (sizeof(ffmpeg_candidates) / sizeof(ffmpeg_candidates[0])); ++i) {
        const char* ffmpeg = ffmpeg_candidates[i];
        if (!ffmpeg || ffmpeg[0] == '\0') {
            continue;
        }
        if (ffmpeg[0] == '/' && access(ffmpeg, X_OK) != 0) {
            continue;
        }

        const char* argv[32];
        size_t n = 0;
        argv[n++] = ffmpeg;
        for (size_t j = 0; argv_common[j] != NULL; ++j) {
            argv[n++] = argv_common[j];
        }
        argv[n] = NULL;

        pid_t pid = -1;
        int spawn_rc = (ffmpeg[0] == '/')
                           ? posix_spawn(&pid, ffmpeg, NULL, NULL, (char* const*)argv, environ)
                           : posix_spawnp(&pid, ffmpeg, NULL, NULL, (char* const*)argv, environ);
        if (spawn_rc != 0) {
            continue;
        }

        int status = 0;
        pid_t waited;
        while ((waited = waitpid(pid, &status, WNOHANG)) == 0 || (waited < 0 && errno == EINTR)) {
            struct stat st;
            bool oversized = load_control && load_control->max_sample_bytes &&
                !stat(output_path, &st) && st.st_size > 0 &&
                (uint64_t)st.st_size > load_control->max_sample_bytes + UINT64_C(65536);
            if (load_cancelled(NULL) || oversized) {
                kill(pid, SIGKILL);
                while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
                return false;
            }
            struct timespec pause = {0, 10000000};
            nanosleep(&pause, NULL);
        }
        if (waited < 0 || load_cancelled(NULL)) return false;
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            return true;
        }
    }

    return false;
}

static bool audio_media_clip_load_mp3_ffmpeg_fallback(const char* path,
                                                      int target_sample_rate,
                                                      AudioMediaClip* out_clip) {
    if (!path || !out_clip || load_cancelled(NULL)) {
        return false;
    }

    char temp_path[] = "/tmp/daw_mp3_fallback_XXXXXX";
    int fd = mkstemp(temp_path);
    if (fd < 0) {
        return false;
    }
    close(fd);

    bool decoded = run_mp3_ffmpeg_decode_to_wav(path, target_sample_rate, temp_path);
    if (!decoded) {
        unlink(temp_path);
        return false;
    }

    bool loaded = audio_media_clip_load_wav(temp_path, target_sample_rate, out_clip);
    unlink(temp_path);
    return loaded;
}

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

// Validates decoded audio and converts it into the requested project sample rate.
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

    if (!load_size_allowed(frame_count, channels, source_rate, desired_rate)) { free(samples); return false; }
    for (size_t n = 0; n < (size_t)frame_count * channels; ++n)
        if ((n % 4096 == 0 && load_cancelled(NULL)) || !isfinite(samples[n])) { free(samples); return false; }
    if (source_rate == desired_rate) {
        out_clip->samples = samples;
        out_clip->frame_count = frame_count;
        out_clip->channels = channels;
        out_clip->sample_rate = source_rate;
        return true;
    }

    float* resampled = NULL;
    uint64_t new_frames = 0;
    bool converted = audio_resample_controlled(samples, frame_count, channels, source_rate, desired_rate,
                                    &resampled, &new_frames, load_cancelled, NULL);
    free(samples);
    if (!converted) return false;
    *out_clip = (AudioMediaClip){.samples = resampled, .frame_count = new_frames,
                               .channels = channels, .sample_rate = desired_rate};
    return true;
}

// Loads bounded little-endian RIFF PCM/float audio while validating chunk and frame structure.
static bool load_wav(const char* path, int target_sample_rate, AudioMediaClip* out_clip, AudioMediaInfo* info) {
    if (!path || (!out_clip && !info)) return false;
    FILE* file = fopen(path, "rb");
    if (!file) return false;
    float* samples = NULL;
    bool ok = false, have_format = false, have_data = false;
    char id[4]; uint32_t riff_size = 0;
    if (fseek(file, 0, SEEK_END)) goto done;
    long file_size = ftell(file);
    if (file_size < 12 || fseek(file, 0, SEEK_SET)) goto done;
    if (fread(id,1,4,file)!=4 || memcmp(id,"RIFF",4) || !read_u32_le(file,&riff_size) ||
        fread(id,1,4,file)!=4 || memcmp(id,"WAVE",4)) goto done;
    uint64_t end = (uint64_t)riff_size + 8;
    if (end < 12 || end > (uint64_t)file_size) goto done;
    uint16_t format = 0, channels = 0, bits = 0, block_align = 0;
    uint32_t rate = 0, byte_rate = 0, data_size = 0;
    uint64_t data_offset = 0;
    for (uint64_t position = 12; position < end;) {
        if (load_cancelled(NULL)) goto done;
        if (end-position < 8 || fseek(file,(long)position,SEEK_SET)) goto done;
        uint32_t size = 0;
        if (fread(id,1,4,file)!=4 || !read_u32_le(file,&size)) goto done;
        uint64_t payload = position+8, next = payload + size + (size & 1u);
        if (next > end) goto done;
        if (!memcmp(id,"fmt ",4)) {
            if (have_format || size < 16) goto done;
            if (!read_u16_le(file,&format) || !read_u16_le(file,&channels) || !read_u32_le(file,&rate) ||
                !read_u32_le(file,&byte_rate) || !read_u16_le(file,&block_align) || !read_u16_le(file,&bits)) goto done;
            if (format == 0xfffe) {
                uint16_t extra, valid_bits; uint32_t mask; unsigned char guid[16];
                static const unsigned char tail[12] = {0,0,16,0,128,0,0,170,0,56,155,113};
                if (size < 40 || !read_u16_le(file,&extra) || extra < 22 || (uint32_t)extra+18 > size ||
                    !read_u16_le(file,&valid_bits) || !read_u32_le(file,&mask) || fread(guid,1,16,file)!=16 ||
                    memcmp(guid+4,tail,12) || guid[1] || guid[2] || guid[3] ||
                    !valid_bits || valid_bits > bits) goto done;
                format = guid[0];
                if (format == 3 && valid_bits != bits) goto done;
                if (mask) {
                    unsigned mapped = 0;
                    for (unsigned bit=0;bit<32;++bit) mapped += (mask>>bit)&1u;
                    if (mapped != channels) goto done;
                }
            }
            have_format = true;
        } else if (!memcmp(id,"data",4)) {
            if (have_data) goto done;
            have_data = true; data_offset = payload; data_size = size;
        }
        position = next;
    }
    if (!have_format || !have_data || !channels || !rate || rate > INT_MAX || !data_size ||
        !((format==1 && (bits==8 || bits==16 || bits==24 || bits==32)) || (format==3 && bits==32))) goto done;
    uint32_t bytes = bits/8;
    uint64_t expected_align = (uint64_t)channels * bytes;
    if (block_align != expected_align || (uint64_t)rate*block_align != byte_rate || data_size%block_align) goto done;
    uint64_t frames = data_size/block_align, total = frames*channels;
    if (!frames || total > SIZE_MAX/sizeof(float) || fseek(file,(long)data_offset,SEEK_SET)) goto done;
    if (info) {
        *info = (AudioMediaInfo){frames, channels, (int)rate};
        ok = true;
        goto done;
    }
    if (!load_size_allowed(frames, channels, (int)rate, target_sample_rate)) goto done;
    samples = malloc((size_t)total*sizeof(float));
    if (!samples) goto done;
    for (uint64_t n=0;n<total;++n) {
        if (n % 4096 == 0 && load_cancelled(NULL)) goto done;
        unsigned char encoded[4]; uint32_t raw = 0;
        if (fread(encoded,1,bytes,file)!=bytes) goto done;
        for (uint32_t b=0;b<bytes;++b) raw |= (uint32_t)encoded[b] << (b*8);
        float value;
        if (format==3) memcpy(&value,&raw,sizeof(value));
        else if (bits==8) value = ((int)raw-128)/128.0f;
        else {
            int64_t signed_sample = (int64_t)raw - ((raw & (1u<<(bits-1))) ? (INT64_C(1)<<bits) : 0);
            value = (float)((double)signed_sample / (double)(INT64_C(1)<<(bits-1)));
        }
        if (!isfinite(value)) goto done;
        samples[n] = value;
    }
    ok = finalize_clip(samples, frames, channels, (int)rate, target_sample_rate, out_clip);
    samples = NULL; // finalize_clip consumes decoded storage on success and failure.
done:
    free(samples); fclose(file); return ok;
}

#if defined(__APPLE__)
static bool audio_media_clip_load_mp3(const char* path, int target_sample_rate, AudioMediaClip* out_clip) {
    if (!path || !out_clip || load_cancelled(NULL)) {
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
        return audio_media_clip_load_mp3_ffmpeg_fallback(path, target_sample_rate, out_clip);
    }

    AudioStreamBasicDescription fileFormat = {0};
    UInt32 formatSize = (UInt32)sizeof(fileFormat);
    status = ExtAudioFileGetProperty(file, kExtAudioFileProperty_FileDataFormat, &formatSize, &fileFormat);
    if (status != noErr || fileFormat.mChannelsPerFrame <= 0) {
        ExtAudioFileDispose(file);
        return audio_media_clip_load_mp3_ffmpeg_fallback(path, target_sample_rate, out_clip);
    }

    SInt64 frameCount = 0;
    UInt32 frameCountSize = (UInt32)sizeof(frameCount);
    status = ExtAudioFileGetProperty(file, kExtAudioFileProperty_FileLengthFrames, &frameCountSize, &frameCount);
    if (status != noErr || frameCount < 0) {
        ExtAudioFileDispose(file);
        return audio_media_clip_load_mp3_ffmpeg_fallback(path, target_sample_rate, out_clip);
    }

    int channels = (int)fileFormat.mChannelsPerFrame;

    Float64 desiredRate = fileFormat.mSampleRate;
    if (!isfinite(desiredRate) || desiredRate < 1 || desiredRate > INT_MAX ||
        channels <= 0 || (unsigned)channels > UINT32_MAX / (4096u * sizeof(float))) {
        ExtAudioFileDispose(file);
        return false;
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
        return audio_media_clip_load_mp3_ffmpeg_fallback(path, target_sample_rate, out_clip);
    }

    uint64_t total_frames = (frameCount > 0) ? (uint64_t)frameCount : 0;
    if (total_frames == 0) {
        total_frames = 4096; /* reserve the complete first unknown-length decode block */
    }

    if (!load_size_allowed(total_frames, channels, (int)desiredRate, target_sample_rate)) { ExtAudioFileDispose(file); return false; }
    float* samples = (float*)malloc((size_t)total_frames * (size_t)channels * sizeof(float));
    if (!samples) {
        ExtAudioFileDispose(file);
        return audio_media_clip_load_mp3_ffmpeg_fallback(path, target_sample_rate, out_clip);
    }

    uint64_t frames_read_total = 0;
    while (true) {
        if (load_cancelled(NULL)) { free(samples); ExtAudioFileDispose(file); return false; }
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
            return audio_media_clip_load_mp3_ffmpeg_fallback(path, target_sample_rate, out_clip);
        }
        if (frames_to_read == 0) {
            break;
        }
        frames_read_total += frames_to_read;
        if (frameCount <= 0) {
            /* extend buffer for streaming files with unknown length */
            if (frames_read_total > SIZE_MAX / sizeof(float) / (size_t)channels - 4096) {
                free(samples); ExtAudioFileDispose(file); return false;
            }
            if (!load_size_allowed(frames_read_total + 4096, channels, (int)desiredRate, target_sample_rate)) {
                free(samples); ExtAudioFileDispose(file); return false;
            }
            float* resized = (float*)realloc(samples, (frames_read_total + 4096) * (size_t)channels * sizeof(float));
            if (!resized) {
                free(samples);
                ExtAudioFileDispose(file);
                return audio_media_clip_load_mp3_ffmpeg_fallback(path, target_sample_rate, out_clip);
            }
            samples = resized;
            total_frames = frames_read_total + 4096;
        }
    }

    ExtAudioFileDispose(file);

    if (frames_read_total == 0) {
        free(samples);
        return audio_media_clip_load_mp3_ffmpeg_fallback(path, target_sample_rate, out_clip);
    }
    size_t used_bytes = (size_t)frames_read_total * (size_t)channels * sizeof(float);
    float* trimmed = (float*)realloc(samples, used_bytes);
    if (trimmed) samples = trimmed;

    return finalize_clip(samples, frames_read_total, channels, (int)desiredRate, target_sample_rate, out_clip);
}
#else
static bool audio_media_clip_load_mp3(const char* path, int target_sample_rate, AudioMediaClip* out_clip) {
    return audio_media_clip_load_mp3_ffmpeg_fallback(path, target_sample_rate, out_clip);
}
#endif

bool audio_media_clip_load(const char* path, int target_sample_rate, AudioMediaClip* out_clip) {
    if (!path || !out_clip || load_cancelled(NULL)) {
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

// Loads WAV samples using the same structural validation as metadata probing.
bool audio_media_clip_load_wav(const char* path, int rate, AudioMediaClip* out) {
    return load_wav(path, rate, out, NULL);
}

// Probes source format without reading or converting the complete sample payload.
bool audio_media_probe(const char* path, AudioMediaInfo* out) {
    if (!path || !out) return false;
    if (load_wav(path, 0, NULL, out)) return true;
    const char* extension = strrchr(path, '.');
    if (!extension || strcasecmp(extension, ".mp3")) return false;
#if defined(__APPLE__)
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(NULL, (const UInt8*)path, strlen(path), false);
    if (!url) return false;
    ExtAudioFileRef file = NULL;
    OSStatus status = ExtAudioFileOpenURL(url, &file);
    CFRelease(url);
    if (status != noErr || !file) return false;
    AudioStreamBasicDescription format = {0};
    SInt64 frames = 0;
    UInt32 size = sizeof(format);
    bool ok = ExtAudioFileGetProperty(file, kExtAudioFileProperty_FileDataFormat, &size, &format) == noErr;
    size = sizeof(frames);
    ok = ok && ExtAudioFileGetProperty(file, kExtAudioFileProperty_FileLengthFrames, &size, &frames) == noErr;
    ExtAudioFileDispose(file);
    if (ok && frames > 0 && format.mChannelsPerFrame > 0 && format.mChannelsPerFrame <= INT_MAX &&
        isfinite(format.mSampleRate) && format.mSampleRate >= 1 && format.mSampleRate <= INT_MAX) {
        *out = (AudioMediaInfo){(uint64_t)frames, (int)format.mChannelsPerFrame, (int)format.mSampleRate};
        return true;
    }
#endif
    return false;
}

// Scopes decode policy to this call and retires any result canceled at the completion boundary.
bool audio_media_clip_load_controlled(const char* path, int rate, AudioMediaClip* out,
                                      const AudioMediaLoadControl* control) {
    if (!out) return false;
    const AudioMediaLoadControl* previous = load_control;
    load_control = control;
    AudioMediaClip result = {0};
    bool ok = audio_media_clip_load(path, rate, &result) && !load_cancelled(NULL);
    load_control = previous;
    if (ok) *out = result;
    else audio_media_clip_free(&result);
    return ok;
}

// Scopes metadata cancellation to one worker request without modifying other callers.
bool audio_media_probe_controlled(const char* path, AudioMediaInfo* out, const AudioMediaLoadControl* control) {
    const AudioMediaLoadControl* previous=load_control; load_control=control;
    AudioMediaInfo info={0}; bool ok=!load_cancelled(NULL) && audio_media_probe(path,&info) && !load_cancelled(NULL);
    load_control=previous; if (ok && out) *out=info; return ok && out;
}
