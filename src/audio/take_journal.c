#define _POSIX_C_SOURCE 200809L
#include "audio/take_journal.h"
#include "audio/wav_writer.h"
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <sys/stat.h>

// Encodes journal integers in a fixed byte order.
static void take_put(unsigned char* data, uint64_t value, int bytes) {
    for (int i = 0; i < bytes; ++i) data[i] = (unsigned char)(value >> (i * 8));
}

// Decodes fixed-width journal integers without alignment assumptions.
static uint64_t take_get(const unsigned char* data, int bytes) {
    uint64_t value = 0;
    for (int i = 0; i < bytes; ++i) value |= (uint64_t)data[i] << (i * 8);
    return value;
}

// Detects incomplete or corrupted records; this is integrity checking rather than authentication.
static uint32_t take_hash(const unsigned char* data, size_t bytes, uint32_t hash) {
    for (size_t i = 0; i < bytes; ++i) hash = (hash ^ data[i]) * 16777619u;
    return hash;
}

// Flushes and synchronizes complete records before advertising a checkpoint.
bool daw_take_journal_sync(DawTakeJournal* journal) {
    return journal && journal->file && !ferror(journal->file) &&
           fflush(journal->file) == 0 && fsync(fileno(journal->file)) == 0;
}

// Closes a journal while retaining its immutable recovery path.
bool daw_take_journal_close(DawTakeJournal* journal) {
    if (!journal || !journal->file) return true;
    bool ok = daw_take_journal_sync(journal);
    if (fclose(journal->file) != 0) ok = false;
    journal->file = NULL;
    return ok;
}

// Creates a unique private file and syncs both its header and directory entry.
bool daw_take_journal_open(DawTakeJournal* journal, const char* directory, int rate, int channels, uint64_t start) {
    if (!journal || !directory || rate <= 0 || channels < 1 || channels > 8) return false;
    memset(journal, 0, sizeof(*journal));
    if (snprintf(journal->path, sizeof(journal->path), "%s/take-XXXXXX", directory) >= (int)sizeof(journal->path)) return false;
    int fd = mkstemp(journal->path);
    if (fd < 0) return false;
    journal->file = fdopen(fd, "w+b");
    if (!journal->file) { close(fd); return false; }
    journal->rate = rate; journal->channels = channels;
    unsigned char header[32] = {0};
    memcpy(header, "DAWTAKE1", 8); take_put(header + 8, (uint32_t)rate, 4);
    take_put(header + 12, (uint32_t)channels, 4); take_put(header + 16, start, 8);
    take_put(header + 24, take_hash(header, 24, 2166136261u), 4);
    bool ok = fwrite(header, 1, sizeof(header), journal->file) == sizeof(header) && daw_take_journal_sync(journal);
    int parent = open(directory, O_RDONLY | O_DIRECTORY);
    if (parent < 0) ok = false;
    else { if (fsync(parent) != 0) ok = false; close(parent); }
    if (!ok) daw_take_journal_close(journal);
    return ok;
}

// Records the software-aligned start before publishing the first audio record.
bool daw_take_journal_set_start(DawTakeJournal* journal, uint64_t start) {
    if (!journal || !journal->file || journal->frames != 0) return false;
    unsigned char header[32] = {0};
    memcpy(header, "DAWTAKE1", 8); take_put(header + 8, (uint32_t)journal->rate, 4);
    take_put(header + 12, (uint32_t)journal->channels, 4); take_put(header + 16, start, 8);
    take_put(header + 24, take_hash(header, 24, 2166136261u), 4);
    return fseek(journal->file, 0, SEEK_SET) == 0 && fwrite(header, 1, 32, journal->file) == 32;
}

// Appends a little-endian float payload and its position/timestamp integrity record.
bool daw_take_journal_append(DawTakeJournal* journal, const float* samples, uint32_t frames, uint64_t timestamp_ns) {
    if (!journal || !journal->file || !samples || !frames || frames > 4096 ||
        journal->frames + frames > (UINT32_MAX - 36u) / (4u * journal->channels)) return false;
    unsigned char payload[4096 * 8 * 4];
    size_t count = (size_t)frames * journal->channels;
    for (size_t i = 0; i < count; ++i) {
        if (!isfinite(samples[i])) return false;
        uint32_t bits; memcpy(&bits, samples + i, 4); take_put(payload + i * 4, bits, 4);
    }
    unsigned char record[32] = {0};
    memcpy(record, "BLK1", 4); take_put(record + 4, frames, 4);
    take_put(record + 8, journal->frames, 8); take_put(record + 16, timestamp_ns, 8);
    uint32_t hash = take_hash(record, 24, 2166136261u);
    take_put(record + 24, take_hash(payload, count * 4, hash), 4);
    if (fwrite(record, 1, 32, journal->file) != 32 || fwrite(payload, 4, count, journal->file) != count) return false;
    journal->frames += frames;
    return true;
}

// Recovers only complete, ordered, checksummed records and preserves the original journal.
DawSaveResult daw_take_journal_publish(const char* source, const char* destination, bool floating,
                                       uint32_t seed, uint64_t required_frames, DawTakeRecoveryInfo* info) {
    if (!source || !destination || !strcmp(source, destination)) return false;
    FILE* file = fopen(source, "rb");
    if (!file) return false;
    struct stat input_info, output_info;
    if (fstat(fileno(file), &input_info) != 0 ||
        (stat(destination, &output_info) == 0 && input_info.st_dev == output_info.st_dev && input_info.st_ino == output_info.st_ino)) {
        fclose(file); return false;
    }
    unsigned char header[32];
    bool ok = fread(header, 1, 32, file) == 32 && !memcmp(header, "DAWTAKE1", 8) &&
              take_get(header + 24, 4) == take_hash(header, 24, 2166136261u);
    DawTakeRecoveryInfo result = {0};
    WavStreamWriter writer = {0};
    float audio[4096 * 8];
    if (ok) {
        result.rate = (int)take_get(header + 8, 4); result.channels = (int)take_get(header + 12, 4);
        result.start_frame = take_get(header + 16, 8);
        ok = result.rate > 0 && result.channels >= 1 && result.channels <= 8;
    }
    if (ok) ok = wav_stream_begin(&writer, destination, result.channels, result.rate, seed, floating);
    while (ok) {
        unsigned char record[32], payload[4096 * 8 * 4];
        size_t read = fread(record, 1, 32, file);
        if (!read) break;
        uint32_t frames = read == 32 ? (uint32_t)take_get(record + 4, 4) : 0;
        if (read != 32 || memcmp(record, "BLK1", 4) || !frames || frames > 4096 ||
            take_get(record + 8, 8) != result.frames) { result.incomplete_tail = true; break; }
        size_t samples = (size_t)frames * result.channels;
        if (fread(payload, 4, samples, file) != samples || take_get(record + 24, 4) !=
            take_hash(payload, samples * 4, take_hash(record, 24, 2166136261u))) { result.incomplete_tail = true; break; }
        if (result.frames + frames > (UINT32_MAX - 36u) / (4u * result.channels)) { ok = false; break; }
        for (size_t i = 0; i < samples; ++i) {
            uint32_t bits = (uint32_t)take_get(payload + i * 4, 4);
            memcpy(audio + i, &bits, 4);
        }
        if (!wav_stream_append(&writer, audio, frames)) { ok = false; break; }
        result.frames += frames;
    }
    if (ferror(file)) ok = false;
    if (fclose(file) != 0) ok = false;
    if (!ok || !result.frames || (required_frames && (result.incomplete_tail || result.frames != required_frames))) {
        wav_stream_abort(&writer);
        return DAW_SAVE_FAILED;
    }
    DawSaveResult published = wav_stream_finish(&writer);
    if (published != DAW_SAVE_FAILED && info) *info = result;
    return published;
}

// Recovers the complete validated prefix with fixed memory while retaining a damaged suffix for inspection.
bool daw_take_journal_recover(const char* source, const char* destination, DawTakeRecoveryInfo* info) {
    return daw_take_journal_publish(source, destination, true, 0, 0, info) == DAW_SAVE_SYNCED;
}
