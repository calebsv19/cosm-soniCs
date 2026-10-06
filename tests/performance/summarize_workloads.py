#!/usr/bin/env python3
"""Summarizes retained workload runs, allowing an explicitly supplied followup to replace fixture cases."""
import argparse
from collections import defaultdict
import json
from pathlib import Path
import statistics


def read_rows(path):
    """Loads immutable sample rows from one runner output directory."""
    return [json.loads(line) for line in (path / 'samples.jsonl').read_text().splitlines()]


def summarize(rows):
    """Reports medians and ranges of per-process metrics without pretending to pool percentile samples."""
    grouped = defaultdict(list)
    for row in rows:
        if row['exit_code']:
            raise ValueError(f"Unresolved workload failure: {row['name']}")
        grouped[row['name']].append(row['measurement'])
    result = {}
    for name, runs in sorted(grouped.items()):
        metrics = {}
        for key, value in runs[0].items():
            if isinstance(value, (float, int)) and all(key in r for r in runs):
                values = [r[key] for r in runs]
                metrics[key] = dict(median=statistics.median(values), minimum=min(values), maximum=max(values))
        result[name] = dict(runs=len(runs), metrics=metrics)
    return result


def main():
    """Writes the complete summary plus explicit exclusions and optimization comparison ratios."""
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--primary', type=Path, required=True)
    p.add_argument('--followup', type=Path)
    p.add_argument('--comparison', type=Path)
    p.add_argument('--output', type=Path, required=True)
    args = p.parse_args()
    primary = read_rows(args.primary)
    followup = read_rows(args.followup) if args.followup else []
    replaced = {r['name'] for r in followup}
    included = [r for r in primary if r['name'] not in replaced] + followup
    data = dict(aggregation='Median/minimum/maximum across per-process statistics; percentiles are not pooled.',
                optimized=summarize(included),
                exclusions=[dict(name=r['name'], repeat=r['repeat'], exit_code=r['exit_code'],
                                 reason='Superseded by explicit followup fixture group.') for r in primary if r['name'] in replaced])
    if args.comparison:
        data['unoptimized'] = summarize(read_rows(args.comparison))
        data['unoptimized_to_optimized_p50_ratio'] = {
            n: v['metrics']['p50_ms']['median'] / data['optimized'][n]['metrics']['p50_ms']['median']
            for n, v in data['unoptimized'].items()}
    args.output.write_text(json.dumps(data, indent=2) + '\n')
    print(f"optimized_workloads={len(data['optimized'])} included_samples={len(included)} excluded_primary_samples={len(data['exclusions'])}")


if __name__ == '__main__':
    main()
