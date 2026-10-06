#!/usr/bin/env python3
"""Qualify anchored capture continuity using fresh SDL dummy-device processes."""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import time


def main():
    """Retains each qualification result and refuses to overwrite prior evidence."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    binary, root = args.binary.resolve(), args.output.resolve()
    root.mkdir(exist_ok=False)
    metadata = dict(created_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                    binary=str(binary), binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
                    notes=['Sequential fresh processes; do not compile during measurements.',
                           'Three 5-second captures at queue profiles 4/32; three 30-second captures at 32.',
                           'Eight FX tracks, actual dummy input/output, one-second UI polling gaps.',
                           'Physical ADC/DAC qualification remains separate.'])
    (root / 'metadata.json').write_text(json.dumps(metadata, indent=2) + '\n')
    failed = 0
    with (root / 'samples.jsonl').open('w') as output:
        for profile, seconds in [(32, 5), (4, 5), (32, 30)]:
            for repeat in range(1, 4):
                print(f'capture profile={profile} seconds={seconds} repeat={repeat}', flush=True)
                command = [str(binary), 'live_record_worker', '8', '1', '0', '1', '128', '48000', str(seconds), '1200']
                row = dict(profile=profile, seconds=seconds, repeat=repeat, command=command)
                began, stderr = time.monotonic(), ''
                try:
                    result = subprocess.run(command, capture_output=True, text=True, timeout=90,
                                            env={**os.environ, 'DAW_BENCH_QUEUE_BLOCKS': str(profile)})
                    stderr = result.stderr
                    row['exit_code'] = result.returncode
                    if result.returncode == 0:
                        measurement = row['measurement'] = json.loads(result.stdout)
                        valid = (measurement['capture_missing_frames'] == measurement['capture_queue_dropped_frames'] ==
                                 measurement['capture_clock_missing_frames'] == measurement['underrun_frames_delta'] == 0 and
                                 measurement['finalized_frames'] == measurement['captured_frames'])
                        if not valid:
                            row.update(exit_code=-1, error='Strict capture continuity/output/duration contract failed.')
                    else:
                        row.update(stdout=result.stdout, error=stderr[-4000:])
                except (subprocess.TimeoutExpired, ValueError, KeyError, OSError) as error:
                    row.update(exit_code=-1, error=str(error))
                row['elapsed_seconds'] = time.monotonic() - began
                failed += row['exit_code'] != 0
                output.write(json.dumps(row) + '\n')
                output.flush()
                if row['exit_code'] == 0:
                    for directory in re.findall(r'^workload_temp_root=(/tmp/daw-s4-workload-[A-Za-z0-9]+)$', stderr, re.M):
                        path = Path(directory)
                        if path.is_dir() and not path.is_symlink():
                            shutil.rmtree(path)
    print(f'capture_clock_matrix_completed=9 failures={failed}', flush=True)
    return int(failed != 0)


if __name__ == '__main__':
    raise SystemExit(main())
