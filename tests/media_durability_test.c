#define _POSIX_C_SOURCE 200809L
#include "audio/wav_writer.h"
#include "audio/take_journal.h"
#include "audio/media_clip.h"
#include "test_assert.h"
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(v,m) daw_test_expect("media_durability_test", (v), (m))
static int fault;
// Models a partial payload/header write.
static size_t media_write(const void* data, size_t size, size_t count, FILE* file) {
    return fwrite(data, size, fault == 1 ? count / 2 : count, file);
}
// Models delayed buffered output failure.
static int media_flush(FILE* file) { return fault == 2 ? EOF : fflush(file); }
// Closes the actual descriptor while exposing a delayed close error.
static int media_close(FILE* file) { int result = fclose(file); return fault == 3 ? EOF : result; }
// Distinguishes file synchronization failure from postpublication directory failure.
static int media_sync(int fd) {
    struct stat st; if (fstat(fd, &st)) return -1;
    if ((fault == 4 && !S_ISDIR(st.st_mode)) || (fault == 6 && S_ISDIR(st.st_mode))) { errno = EIO; return -1; }
    return fsync(fd);
}
// Rejects publication without touching the preceding destination.
static int media_rename(const char* from, const char* to) { return fault == 5 ? -1 : rename(from, to); }
#define fwrite media_write
#define fflush media_flush
#define fclose media_close
#define fsync media_sync
#define rename media_rename
#include "../src/session/save_file.c"
#include "../src/audio/wav_writer.c"
#undef fwrite
#undef fflush
#undef fclose
#undef fsync
#undef rename

// Checks decoded content rather than treating a write return code as media proof.
static void expect_audio(const char* path, uint64_t frames, float expected) {
    AudioMediaClip clip = {0};
    CHECK(audio_media_clip_load_wav(path, 48000, &clip), "WAV decode");
    CHECK(clip.frame_count == frames && fabsf(clip.samples[0] - expected) < 0.0001f, "wrong WAV content");
    audio_media_clip_free(&clip);
}

// Verifies atomic media failures and recovery of an ordered checksummed prefix with a torn tail.
int main(void) {
    char root[] = "/tmp/daw-media-durable-XXXXXX";
    CHECK(mkdtemp(root) != NULL, "fixture directory");
    char wav[1024], recovered[1024];
    snprintf(wav, sizeof(wav), "%s/audio.wav", root);
    snprintf(recovered, sizeof(recovered), "%s/recovered.wav", root);
    float old[16], next[16];
    for (int i = 0; i < 16; ++i) { old[i] = 0.25f; next[i] = 0.5f; }
    CHECK(wav_write_f32(wav, old, 16, 1, 48000), "initial WAV");
    for (fault = 1; fault <= 5; ++fault) {
        CHECK(!wav_write_pcm16(wav, next, 16, 1, 48000), "failed media write reported success");
        expect_audio(wav, 16, 0.25f);
    }
    CHECK(wav_write_pcm16_dithered_result(wav, next, 16, 1, 48000, 0) == DAW_SAVE_PUBLISHED, "directory failure classification");
    fault = 0;
    expect_audio(wav, 16, 0.5f);
    CHECK(!wav_write_f32(wav, old, UINT64_MAX, 1, 48000), "RIFF overflow accepted");
    next[0] = NAN;
    CHECK(!wav_write_f32(wav, next, 16, 1, 48000), "nonfinite media accepted");
    expect_audio(wav, 16, 0.5f);
    DawTakeJournal journal;
    CHECK(daw_take_journal_open(&journal, root, 48000, 1, 1200), "journal creation");
    CHECK(daw_take_journal_set_start(&journal, 2400), "journal placement");
    CHECK(daw_take_journal_append(&journal, old, 16, 123000) && daw_take_journal_sync(&journal), "checkpoint");
    CHECK(daw_take_journal_close(&journal), "checkpoint close");
    CHECK(daw_take_journal_publish(journal.path, wav, false, 123, 32, NULL) == DAW_SAVE_FAILED,
          "record-aligned truncation was published as a complete take");
    expect_audio(wav, 16, 0.5f);
    FILE* torn = fopen(journal.path, "ab");
    CHECK(torn && fwrite("BLK1broken", 1, 10, torn) == 10 && fclose(torn) == 0, "torn suffix");
    DawTakeRecoveryInfo info;
    CHECK(daw_take_journal_recover(journal.path, recovered, &info), "recover prefix");
    CHECK(info.frames == 16 && info.start_frame == 2400 && info.incomplete_tail, "recovery metadata");
    expect_audio(recovered, 16, 0.25f);
    CHECK(daw_take_journal_publish(journal.path, recovered, false, 123, 16, NULL) == DAW_SAVE_FAILED,
          "strict finalization published a truncated take");
    expect_audio(recovered, 16, 0.25f);
    float multichannel[1031 * 3];
    for (size_t i = 0; i < sizeof(multichannel) / sizeof(float); ++i) multichannel[i] = sinf((float)i) * 1.1f;
    CHECK(wav_write_pcm16_dithered(wav, multichannel, 1031, 3, 48000, 123), "reference PCM");
    WavStreamWriter stream;
    CHECK(wav_stream_begin(&stream, recovered, 3, 48000, 123, false), "stream begin");
    CHECK(wav_stream_append(&stream, multichannel, 17) &&
          wav_stream_append(&stream, multichannel + 17 * 3, 1014), "uneven stream chunks");
    CHECK(wav_stream_finish(&stream) == DAW_SAVE_SYNCED, "stream finish");
    FILE* reference = fopen(wav, "rb");
    FILE* streamed = fopen(recovered, "rb");
    CHECK(reference && streamed, "parity files");
    int byte;
    do { byte = fgetc(reference); CHECK(byte == fgetc(streamed), "dither chunk parity"); } while (byte != EOF);
    fclose(reference); fclose(streamed);
    CHECK(wav_stream_begin(&stream, recovered, 1, 48000, 0, false), "failed append begin");
    CHECK(wav_stream_append(&stream, old, 16), "initial append");
    CHECK(!wav_stream_append(&stream, next, 16), "late nonfinite append accepted");
    CHECK(wav_stream_finish(&stream) == DAW_SAVE_FAILED, "failed stream published");
    CHECK(!daw_take_journal_recover(journal.path, journal.path, &info), "overwrote journal");
    char alias[1100]; snprintf(alias, sizeof(alias), "%s/./%s", root, strrchr(journal.path, '/') + 1);
    CHECK(!daw_take_journal_recover(journal.path, alias, &info), "aliased journal overwritten");
    CHECK(unlink(wav) == 0 && unlink(recovered) == 0 && unlink(journal.path) == 0 && rmdir(root) == 0, "fixture cleanup");
    puts("media_durability_test: success (5 prepublication failures, directory uncertainty, RIFF limits, torn journal recovery)");
    return 0;
}
