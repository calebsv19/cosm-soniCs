#!/usr/bin/env python3
"""Retain six fresh-process media-import workload receipts without overwriting an earlier run."""
import argparse
import datetime
import hashlib
import json
import pathlib
import subprocess
import time


def main():
    """Run the native/converted matrix serially and retain both successful and failed case output."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True, type=pathlib.Path)
    parser.add_argument('--output', required=True, type=pathlib.Path)
    args = parser.parse_args()
    binary = args.binary.resolve(strict=True)
    args.output.mkdir(parents=True, exist_ok=False)
    receipt = {
        'created_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'binary': str(binary),
        'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
        'samples': [],
    }
    for rate in (48000, 44100):
        for repeat in range(1, 4):
            command = [str(binary), str(rate)]
            started = time.monotonic()
            row = {'rate': rate, 'repeat': repeat, 'command': command}
            try:
                result = subprocess.run(command, capture_output=True, text=True, timeout=90)
                row['exit_code'] = result.returncode
                stdout, stderr = result.stdout, result.stderr
            except subprocess.TimeoutExpired as exc:
                row['exit_code'] = None
                row['error'] = '90-second workload timeout'
                stdout = exc.stdout or b''
                stderr = exc.stderr or b''
                stdout = stdout.decode(errors='replace') if isinstance(stdout, bytes) else stdout
                stderr = stderr.decode(errors='replace') if isinstance(stderr, bytes) else stderr
            log = args.output / f'{rate}-{repeat}.log'
            log.write_text(stdout + stderr)
            row['log'] = str(log)
            row['log_sha256'] = hashlib.sha256(log.read_bytes()).hexdigest()
            row['elapsed_seconds'] = time.monotonic() - started
            try:
                row['measurements'] = [json.loads(line) for line in stdout.splitlines() if line.startswith('{')]
                retired = [m for m in row['measurements'] if m.get('phase') == 'retired']
                row['accepted'] = row['exit_code'] == 0 and len(retired) == 1 and all(
                    retired[0][key] == 0 for key in ('capture_missing_frames', 'output_missing_frames')
                ) and retired[0]['pinned_baseline_bytes'] == retired[0]['pinned_after_bytes']
            except (ValueError, KeyError) as exc:
                row['error'] = str(exc)
                row['accepted'] = False
            receipt['samples'].append(row)
            (args.output / 'workloads.json').write_text(json.dumps(receipt, indent=2) + '\n')
            print(rate, repeat, 'passed' if row['accepted'] else 'failed', flush=True)
            if not row['accepted']:
                return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
