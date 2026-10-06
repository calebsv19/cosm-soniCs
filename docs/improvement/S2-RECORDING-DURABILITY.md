# S2.5 — Recording placement and durable media

## 1. Behavior contract

1. Timeline capture accepts the project sample rate and a fixed input channel layout. Callback packets carry source sample offsets, arrival timestamps, and a software presentation-clock observation. The first accepted callback anchors the take to its observed presentation end minus callback length; subsequent placement follows the input sample count. This is nominal software alignment, not calibrated ADC/DAC latency compensation. The latest difference between sample-count placement and output observation is retained for diagnostics.
2. Capture before playback is ignored. A take belongs to one continuous transport epoch. Pause, stop, seek, or loop changes halt further capture and retain the prefix for Finish. Starting timeline recording with looping enabled is rejected. Loop comping and discontinuous takes are separate work. Raw/synthetic APIs retain explicit caller placement and gating.
3. Queue overflow advances the source sample counter. Drain inserts silence for missing intervals, including a dropped suffix on Finish, so following audio does not slide earlier. Callback code performs bounded packet copies and atomic operations; it never writes files, allocates, or logs.
4. Ordered, timestamped, checksummed float records are appended to a unique private `recordings/take-*` journal and synchronized before checkpoint publication. A checkpoint failure halts capture and preserves the valid prefix for recovery. [S4.6](S4-STREAMING.md) now gives production timeline capture a journal worker and bounded recent preview; raw/synthetic callers retain explicit synchronous drains. Finalization and recovery stream records with fixed buffers. Finished-media import/cache is separate S4.7 work.
5. WAV publication uses the S2.1 staged writer: checked writes, flush, file sync, close, atomic rename, and directory sync. Recording requires fully synced media before registry/clip insertion. A post-rename directory-sync failure can leave visible media but reports failure and retains the take for retry. Existing WAVs survive failures before rename. RIFF-size overflow and nonfinite samples are rejected.
6. Failed final WAV publication or insertion preserves the take and journal. A new recording cannot silently replace retained audio. Explicit cancel releases RAM/device state but retains the journal. Successful insertion also retains the journal because project publication may happen later. Recovery files are not automatically deleted.

## 2. Recovery

Run the application without opening the desktop:

```sh
build/targets/macOS-arm64/toolchains/clang/bin/daw_app --recover-take /absolute/path/recordings/take-XXXXXX /absolute/path/recovered.wav
```

Recovery verifies the header and each ordered record, stops at the first torn/corrupt tail, and atomically writes the complete prefix as float WAV. It reports frame count, rate, channels, original timeline start, and whether a tail was ignored. It preserves the source and refuses source/destination aliases. Import the recovered WAV and place it at the reported start frame. Journal discovery, a recovery browser, automatic project reinsertion, hardware latency calibration, and power-cut testing are not implemented here. Recovery currently loads the prefix into memory.

## 3. Acceptance

- `test-media-durability`: partial write, flush, close, file-sync, rename, directory-sync outcomes; preceding WAV preservation; RIFF overflow/nonfinite rejection; checksummed journal recovery with torn tail and alias rejection.
- `test-audio-recording`: exact software placement, pre-play gating, overflow silence, epoch halt, failed publication retention/retry, recovered checkpoint, original track identity, and existing dummy-device/undo/session integration.
- Session atomic-save and coherent restore regression checks remain required alongside recording checks.

These are deterministic software/file-system and SDL dummy-device checks. They do not establish physical recording latency or microphone quality. Final verification commands and hashes are recorded in the S2.5/S2.6 evidence receipt.
