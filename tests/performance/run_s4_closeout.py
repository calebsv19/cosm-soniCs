#!/usr/bin/env python3
"""Runs bounded S4 closeout profiles while retaining the older strict timing verdict separately."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess


def main():
    """Preserves all results and distinguishes callback continuity from worker-budget observations."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    binary = args.binary.resolve()
    args.output.mkdir(parents=True, exist_ok=False)
    cases = [('heavy_dummy96', ['dummy', 96000, 128, 32, 120, 1, 32]),
             ('paced96', ['paced', 96000, 128, 32, 60, 1, 32]),
             ('mixed128', ['mixed', 48000, 128, 8, 120, 1, 32]),
             ('mixed512', ['mixed', 48000, 512, 8, 60, 1, 32])]
    rows = []
    for name, dimensions in cases:
        print(name + ' started', flush=True)
        with (args.output / (name + '.jsonl')).open('w') as out, (args.output / (name + '.stderr')).open('w') as err:
            try:
                code = subprocess.run([str(binary), *map(str, dimensions)], stdout=out, stderr=err,
                                      env={**os.environ, 'DAW_BENCH_UI': '1'}, timeout=dimensions[4] + 120).returncode
            except subprocess.TimeoutExpired:
                code = -1
        observations = [json.loads(line) for line in (args.output / (name + '.jsonl')).read_text().splitlines()
                        if line.startswith('{') and line.endswith('}')]
        summaries = [row for row in observations if row['type'] == 'summary']
        functional = code == 0 and len(summaries) == 1
        summary = summaries[0] if summaries else {}
        continuity = functional and all(summary[key] == 0 for key in
            ['output_missing_frames', 'capture_missing_frames', 'spectrum_dropped', 'spectrogram_dropped'])
        strict = continuity and summary['worker_over_budget'] == summary['paced_late_periods'] == 0
        rows.append({'name': name, 'dimensions': dimensions, 'exit_code': code,
                     'workflow_pass': functional, 'continuity_pass': continuity,
                     'strict_timing_pass': strict, 'observations': observations})
        receipt = {'binary': str(binary), 'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
                   'ui_notification_path_enabled': True, 'cases': rows,
                   'scope': 'Nonexclusive host, optimized software profiles; no physical listening or universal deadline guarantee'}
        (args.output / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
        print(name + ': ' + json.dumps({k: rows[-1][k] for k in
              ['workflow_pass', 'continuity_pass', 'strict_timing_pass']}), flush=True)
    # Deliberately preserve the original strict process-exit gate as well as per-case qualified results.
    return int(not all(row['strict_timing_pass'] for row in rows))


if __name__ == '__main__':
    raise SystemExit(main())
