#!/usr/bin/env python3
"""Retains sequential matched analyzer measurements and bounded live acceptance receipts."""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import platform
import random
import subprocess


def main():
    """Runs three fresh processes per configuration and keeps failed results visible."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--kernel', type=Path, required=True)
    parser.add_argument('--live', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--controls-only', action='store_true', help='Run matching playback with analyzers disabled')
    parser.add_argument('--live-only', action='store_true')
    parser.add_argument('--tracks', type=int, choices=(8, 32), default=32)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    binaries = {name: path.resolve() for name, path in [('kernel', args.kernel), ('live', args.live)]}
    metadata = {'created': datetime.datetime.now(datetime.timezone.utc).isoformat(),
                'platform': platform.platform(), 'queue_blocks': 32,
                'binaries': {name: {'path': str(path), 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
                             for name, path in binaries.items()}}
    (args.output / 'metadata.json').write_text(json.dumps(metadata, indent=2) + '\n')
    jobs = []
    for repeat in range(3):
        for rate in (44100, 48000, 96000):
            for frames in (1024, 2048):
                jobs.append(('kernel', repeat, [rate, frames]))
        for rate in (48000, 96000):
            jobs.append(('live', repeat, ['live_analysis', args.tracks, 1, 0, 1, 128, rate, 8, 1200]))
    if args.controls_only:
        jobs = [('live', repeat, ['live', args.tracks, 1, 0, 1, 128, rate, 8, 1200])
                for repeat in range(3) for rate in (48000, 96000)]
    if args.live_only:
        jobs = [job for job in jobs if job[0] == 'live']
    random.Random(48).shuffle(jobs)
    failures = 0
    with (args.output / 'samples.jsonl').open('w') as stream:
        for index, (kind, repeat, dimensions) in enumerate(jobs):
            command = [str(binaries[kind]), *map(str, dimensions)]
            row = {'kind': kind, 'repeat': repeat + 1, 'command': command}
            print(f'{index + 1}/{len(jobs)} {kind} {dimensions}', flush=True)
            try:
                result = subprocess.run(command, text=True, capture_output=True, timeout=120,
                                        env={**os.environ, 'DAW_BENCH_QUEUE_BLOCKS': '32'})
                (args.output / f'{index:02}.stdout.log').write_text(result.stdout)
                (args.output / f'{index:02}.stderr.log').write_text(result.stderr)
                row['exit_code'] = result.returncode
                if result.returncode:
                    row['accepted'] = False
                else:
                    data = row['measurement'] = json.loads(result.stdout)
                    if kind == 'kernel':
                        row['accepted'] = (data['max_error_db'] <= .002 and
                                           data['prepared_cpu_ms'] < data['reference_cpu_ms'])
                    else:
                        row['accepted'] = (data['underrun_frame_delta'] == 0 and
                            data['worker_over_budget_delta'] == 0 and
                            data['spectrum_dropped_delta'] == data['spectrogram_dropped_delta'] == 0 and
                            (args.controls_only or (data['spectrum_published_delta'] > 0 and data['spectrogram_published_delta'] > 0)))
            except (subprocess.TimeoutExpired, ValueError) as exc:
                row.update(accepted=False, error=str(exc))
            failures += not row['accepted']
            stream.write(json.dumps(row) + '\n')
            stream.flush()
    print(f'completed={len(jobs)} failures={failures}', flush=True)
    return int(failures != 0)


if __name__ == '__main__':
    raise SystemExit(main())
