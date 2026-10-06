#!/usr/bin/env python3
"""Retains repeated integrated lifetimes and checks a declared warm-process memory envelope."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def main():
    """Measures cleanup growth after warmup without conflating allocator residency and owned media."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--cycles', type=int, default=20)
    args = parser.parse_args()
    if not 12 <= args.cycles <= 60:
        parser.error('Use 12..60 cycles so the first ten can establish warm residency')
    args.output.mkdir(parents=True, exist_ok=False)
    binary = args.binary.resolve()
    with (args.output / 'stdout.jsonl').open('w') as out, (args.output / 'stderr.log').open('w') as err:
        try:
            code = subprocess.run([str(binary), str(args.cycles)], stdout=out, stderr=err,
                                  timeout=args.cycles * 40 + 60).returncode
        except subprocess.TimeoutExpired:
            code = -1
    rows = [json.loads(line) for line in (args.output / 'stdout.jsonl').read_text().splitlines()
            if line.startswith('{') and line.endswith('}')]
    cleanup = [row for row in rows if row['type'] == 'heap' and row['phase'] == 'after_cleanup']
    summaries = [row for row in rows if row['type'] == 'summary']
    complete = code == 0 and len(cleanup) == len(summaries) == args.cycles
    # Fixed fixture envelope: at most 8 MiB extra live malloc and 64 MiB extra RSS after cycle ten.
    # This is workload qualification, not a proof of zero leaks or an arbitrary project memory cap.
    memory = complete and all(row['in_use_bytes'] <= cleanup[9]['in_use_bytes'] + 8 * 1024**2 and
                              row['rss_bytes'] <= cleanup[9]['rss_bytes'] + 64 * 1024**2
                              for row in cleanup[10:])
    continuity = complete and all(row['output_missing_frames'] == row['capture_missing_frames'] ==
                                  row['spectrum_dropped'] == row['spectrogram_dropped'] == 0 for row in summaries)
    receipt = {'binary': str(binary), 'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
               'cycles': args.cycles, 'exit_code': code, 'workflow_pass': complete,
               'continuity_pass': continuity, 'warm_memory_envelope_pass': memory,
               'envelope': {'warmup_cycles': 10, 'additional_live_malloc_bytes': 8*1024**2,
                            'additional_rss_bytes': 64*1024**2}, 'observations': rows}
    (args.output / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(json.dumps({k: v for k, v in receipt.items() if k != 'observations'}), flush=True)
    return int(not (complete and continuity and memory))


if __name__ == '__main__':
    raise SystemExit(main())
