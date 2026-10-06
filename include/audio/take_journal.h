#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "daw/save_file.h"

// Owns an append-only take checkpoint; closing never deletes recoverable audio.
typedef struct DawTakeJournal {
    FILE* file;
    uint64_t frames;
    int rate, channels;
    char path[1024];
} DawTakeJournal;

// Describes the validated prefix recovered from an interrupted take.
typedef struct DawTakeRecoveryInfo {
    uint64_t frames, start_frame;
    int rate, channels;
    bool incomplete_tail;
} DawTakeRecoveryInfo;

// Creates and synchronizes a unique private journal beside the intended recording.
bool daw_take_journal_open(DawTakeJournal* journal, const char* directory, int rate, int channels, uint64_t start);
// Updates the placement anchor before any audio has been checkpointed.
bool daw_take_journal_set_start(DawTakeJournal* journal, uint64_t start);
// Appends a checksummed bounded record without doing callback-thread I/O.
bool daw_take_journal_append(DawTakeJournal* journal, const float* samples, uint32_t frames, uint64_t timestamp_ns);
// Makes all completely appended records durable to the filesystem boundary.
bool daw_take_journal_sync(DawTakeJournal* journal);
// Closes the handle and retains the journal path for explicit recovery.
bool daw_take_journal_close(DawTakeJournal* journal);
// Converts the validated journal prefix into an atomically published WAV without modifying the journal.
bool daw_take_journal_recover(const char* source, const char* wav_path, DawTakeRecoveryInfo* info);

// Streams a journal into WAV and optionally requires an exact complete frame count before publication.
DawSaveResult daw_take_journal_publish(const char* source, const char* destination, bool floating,
                                       uint32_t seed, uint64_t required_frames, DawTakeRecoveryInfo* info);
