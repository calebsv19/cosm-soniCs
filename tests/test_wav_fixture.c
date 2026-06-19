#include "test_wav_fixture.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

static void write_u16_le(FILE* fp, uint16_t value) {
    unsigned char bytes[2];
    bytes[0] = (unsigned char)(value & 0xFFu);
    bytes[1] = (unsigned char)((value >> 8) & 0xFFu);
    fwrite(bytes, 1, sizeof(bytes), fp);
}

static void write_u32_le(FILE* fp, uint32_t value) {
    unsigned char bytes[4];
    bytes[0] = (unsigned char)(value & 0xFFu);
    bytes[1] = (unsigned char)((value >> 8) & 0xFFu);
    bytes[2] = (unsigned char)((value >> 16) & 0xFFu);
    bytes[3] = (unsigned char)((value >> 24) & 0xFFu);
    fwrite(bytes, 1, sizeof(bytes), fp);
}

static int ensure_tmp_dir(void) {
    return mkdir("tmp", 0755) == 0 || errno == EEXIST;
}

int daw_test_wav_write_silence(const char* path, int sample_rate, uint32_t frames) {
    if (!path || path[0] == '\0' || sample_rate <= 0) {
        return 0;
    }
    if (!ensure_tmp_dir()) {
        return 0;
    }

    const uint16_t channels = 1;
    const uint16_t bits_per_sample = 16;
    const uint16_t block_align = (uint16_t)(channels * (bits_per_sample / 8));
    const uint32_t byte_rate = (uint32_t)sample_rate * (uint32_t)block_align;
    const uint32_t data_size = frames * (uint32_t)block_align;
    const uint32_t riff_size = 36u + data_size;

    FILE* fp = fopen(path, "wb");
    if (!fp) {
        return 0;
    }

    fwrite("RIFF", 1, 4, fp);
    write_u32_le(fp, riff_size);
    fwrite("WAVE", 1, 4, fp);
    fwrite("fmt ", 1, 4, fp);
    write_u32_le(fp, 16u);
    write_u16_le(fp, 1u);
    write_u16_le(fp, channels);
    write_u32_le(fp, (uint32_t)sample_rate);
    write_u32_le(fp, byte_rate);
    write_u16_le(fp, block_align);
    write_u16_le(fp, bits_per_sample);
    fwrite("data", 1, 4, fp);
    write_u32_le(fp, data_size);

    for (uint32_t i = 0; i < frames; ++i) {
        write_u16_le(fp, 0u);
    }

    return fclose(fp) == 0;
}

void daw_test_wav_write_silence_or_fail(const char* path,
                                        int sample_rate,
                                        uint32_t frames,
                                        const char* test_name) {
    if (daw_test_wav_write_silence(path, sample_rate, frames)) {
        return;
    }
    fprintf(stderr,
            "%s: failed to create wav fixture (%s)\n",
            test_name ? test_name : "daw_test",
            path ? path : "");
    exit(1);
}
