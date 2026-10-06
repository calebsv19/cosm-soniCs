#!/usr/bin/env python3
"""Runs bounded workload samples sequentially and preserves measurements, failures, and host context."""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import platform
import random
import re
import shutil
import subprocess
import time


def workloads():
    """Defines matched workload pairs so track, source, note, effect, and block costs can be compared."""
    cases = []
    def add(name, mode='render', tracks=8, clips=1, notes=0, effects=0, block=128, rate=48000, seconds=8, iterations=1200):
        """Adds one workload with all dimensions explicit in the emitted metadata."""
        cases.append({'name': name, 'args': [mode, tracks, clips, notes, effects, block, rate, seconds, iterations]})
    for tracks in (8, 32, 64):
        add(f'audio_{tracks}', tracks=tracks)
        add(f'fx_{tracks}', tracks=tracks, effects=1)
    for tracks in (8, 32):
        add(f'sparse_{tracks}x32', tracks=tracks, clips=32)
    add('overlap_8x8', mode='render_overlap', clips=8)
    add('midi_8x8', notes=8)
    add('midi_8x32', notes=32)
    add('midi_sparse_1x1', mode='render_sparse', tracks=1, notes=1)
    add('midi_sparse_1x128', mode='render_sparse', tracks=1, notes=128)
    add('midi_sparse_1x1024', mode='render_sparse', tracks=1, notes=1024)
    add('fx_32_block64', tracks=32, effects=1, block=64)
    add('fx_32_block512', tracks=32, effects=1, block=512)
    add('fx_32_rate96k', tracks=32, effects=1, rate=96000)
    add('analysis_kernel', mode='analysis')
    add('live_fx32', mode='live', tracks=32, effects=1)
    add('live_fx32_analysis', mode='live_analysis', tracks=32, effects=1)
    add('live_midi8x32', mode='live', notes=32)
    add('live_fx32_export', mode='live_export', tracks=32, effects=1, seconds=30)
    add('live_sparse32_edits', mode='live_edits', tracks=32, clips=32)
    add('live_sparse32_mixed_edits', mode='live_mixed_edits', tracks=32, clips=32)
    add('live_fx8_record', mode='live_record', effects=1, seconds=10)
    add('record_10s', mode='record', tracks=1, seconds=10)
    add('record_60s', mode='record', tracks=1, seconds=60)
    add('export_10s', mode='export', tracks=32, seconds=10)
    add('export_120s', mode='export', tracks=32, seconds=120)
    add('export_stream_10s', mode='export_stream', tracks=32, seconds=10)
    add('export_stream_120s', mode='export_stream', tracks=32, seconds=120)
    add('live_fx32_export_stream', mode='live_export_stream', tracks=32, effects=1, seconds=30)
    add('live_fx8_record_worker', mode='live_record_worker', effects=1, seconds=5)
    add('import_30s_native', mode='import', tracks=1, seconds=30)
    add('import_30s_convert', mode='import', tracks=1, block=64, seconds=30)
    return cases


def main():
    """Persists each sample immediately and fails visibly if any workload or output contract fails."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--repeats', type=int, default=3, choices=range(1, 6))
    parser.add_argument('--group', choices=('full', 'comparison', 'smoke', 'followup', 'scheduling', 'queue', 'streaming'), default='full')
    parser.add_argument('--queue-blocks', type=int, choices=(4, 8, 32), default=32)
    args = parser.parse_args()
    binary = args.binary.resolve()
    selected = workloads()
    if args.group == 'streaming':
        selected = [c for c in selected if c['name'] in ('record_10s', 'record_60s', 'export_10s', 'export_120s', 'export_stream_10s', 'export_stream_120s', 'live_fx32_export_stream', 'live_fx8_record_worker')]
    if args.group == 'comparison':
        selected = [c for c in selected if c['name'] in ('audio_32', 'fx_32', 'midi_8x32', 'sparse_32x32', 'analysis_kernel')]
    if args.group == 'queue':
        selected = [c for c in selected if c['name'] in ('live_fx32_analysis', 'live_midi8x32', 'live_fx32_export', 'live_sparse32_edits', 'live_sparse32_mixed_edits', 'live_fx8_record')]
    if args.group == 'scheduling':
        selected = [c for c in selected if c['name'] in ('audio_32', 'fx_32', 'sparse_32x32', 'overlap_8x8', 'midi_8x8', 'midi_8x32', 'midi_sparse_1x1', 'midi_sparse_1x128', 'midi_sparse_1x1024', 'live_midi8x32', 'live_sparse32_edits')]
    if args.group == 'followup':
        selected = [c for c in selected if c['name'] in ('midi_sparse_1x1', 'midi_sparse_1x128', 'midi_sparse_1x1024', 'overlap_8x8')]
    if args.group == 'smoke':
        selected = [c for c in selected if c['name'] in ('audio_8', 'midi_sparse_1x128', 'live_fx32_analysis', 'record_10s', 'import_30s_convert')]
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    if (output / 'samples.jsonl').exists():
        parser.error('Choose a new output directory; existing measurements are never overwritten.')
    meta = dict(started_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(), binary=str(binary),
                binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(), platform=platform.platform(),
                machine=platform.machine(), logical_cpus=os.cpu_count(), load_average=os.getloadavg(),
                repeats=args.repeats, group=args.group, queue_blocks=args.queue_blocks, order_seed=437, workloads=selected,
                notes=['Sequential fresh processes; no CPU pinning or exclusive host access.',
                       'macOS peak RSS units are bytes; peak includes fixture construction.',
                       'Dummy callbacks do not measure physical DAC deadlines.',
                       'live_record_worker uses actual SDL dummy input/output with a one-second UI polling gap.',
                       'Recording feeds synthetic 100ms packets; offline recording cases are accelerated, live_record waits 100ms per drain.'])
    (output / 'metadata.json').write_text(json.dumps(meta, indent=2) + '\n')
    jobs = [(r, c) for r in range(args.repeats) for c in selected]
    # Shuffle within the full sequence reproducibly to reduce ordering/thermal bias.
    random.Random(437).shuffle(jobs)
    failed = 0
    with (output / 'samples.jsonl').open('w') as stream:
        for index, (repeat, case) in enumerate(jobs):
            command = [str(binary), *map(str, case['args'])]
            start = time.monotonic()
            print(f"[{index + 1}/{len(jobs)}] {case['name']} repeat={repeat + 1}", flush=True)
            try:
                result = subprocess.run(command, capture_output=True, text=True, timeout=120,
                                        env={**os.environ, "DAW_BENCH_QUEUE_BLOCKS": str(args.queue_blocks)})
                row = dict(name=case['name'], repeat=repeat + 1, exit_code=result.returncode,
                           elapsed_seconds=time.monotonic() - start, command=command)
                (output / f"{case['name']}-{repeat + 1}.stderr.log").write_text(result.stderr)
                if result.returncode == 0:
                    row['measurement'] = json.loads(result.stdout)
                else:
                    row['stdout'] = result.stdout
                    row['error'] = result.stderr[-2000:]
                for root in re.findall(r'^workload_temp_root=(/tmp/daw-s4-workload-[A-Za-z0-9]+)$', result.stderr, re.M):
                    target = Path(root)
                    if target.is_dir() and not target.is_symlink():
                        shutil.rmtree(target)
            except (subprocess.TimeoutExpired, ValueError) as exc:
                row = dict(name=case['name'], repeat=repeat + 1, exit_code=-1, error=str(exc))
            failed += row['exit_code'] != 0
            stream.write(json.dumps(row) + '\n')
            stream.flush()
    print(f"completed={len(jobs)} failures={failed} output={output}", flush=True)
    return 1 if failed else 0


if __name__ == '__main__':
    raise SystemExit(main())
