#!/usr/bin/env python3
"""Runs the bounded S4.9 matrix serially and retains all workflow and strict timing failures."""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import time


def main():
    """Records binary identity, interval samples, and distinct functionality/timing dispositions."""
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--smoke',action='store_true')
    args=parser.parse_args();binary=args.binary.resolve();args.output.mkdir(parents=True,exist_ok=False)
    cases=[('dummy32_48',['dummy',48000,128,32,60,1,32]),
           ('paced32_48',['paced',48000,128,32,120,1,32]),
           ('mixed8_48_128',['mixed',48000,128,8,120,1,32]),
           ('dummy32_96',['dummy',96000,128,32,60,1,32]),
           ('control_dummy32_96',['dummy',96000,128,32,60,0,32]),
           ('paced32_96',['paced',96000,128,32,60,1,32]),
           ('control_paced32_96',['paced',96000,128,32,60,0,32]),
           ('mixed8_48_512',['mixed',48000,512,8,120,1,32])]
    if args.smoke: cases=[('mixed_smoke',['mixed',48000,128,8,10,1,32])]
    metadata={'created_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'platform':platform.platform(),
              'binary':str(binary),'binary_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),
              'cases':cases,'scope':'Nonexclusive host; software consumers only; no physical timing or overnight claim'}
    (args.output/'metadata.json').write_text(json.dumps(metadata,indent=2)+'\n')
    failures=0
    for index,(name,dimensions) in enumerate(cases):
        command=[str(binary),*map(str,dimensions)];print(f'{index+1}/{len(cases)} {name} started',flush=True)
        began=time.monotonic();row={'name':name,'command':command}
        out=args.output/(name+'.stdout.jsonl');err=args.output/(name+'.stderr.log')
        try:
            with out.open('w') as stdout,err.open('w') as stderr:
                result=subprocess.run(command,stdout=stdout,stderr=stderr,timeout=dimensions[4]+120)
            observations=[json.loads(line) for line in out.read_text().splitlines() if line.startswith('{')]
            row.update(exit_code=result.returncode,observations=observations,wall_seconds=time.monotonic()-began)
            summaries=[x for x in observations if x['type']=='summary']
            ownership=[x for x in observations if x['type']=='ownership']
            row['workflow_pass']=result.returncode==0 and len(summaries)==1 and len(ownership)==1
            if row['workflow_pass']:
                s=summaries[0]
                row['continuity_pass']=s['output_missing_frames']==s['capture_missing_frames']==s['spectrum_dropped']==s['spectrogram_dropped']==0
                row['strict_timing_pass']=row['continuity_pass'] and s['worker_over_budget']==0 and s['paced_late_periods']==0
            else: row.update(continuity_pass=False,strict_timing_pass=False)
        except (subprocess.TimeoutExpired,ValueError) as exc:
            row.update(workflow_pass=False,continuity_pass=False,strict_timing_pass=False,error=str(exc))
        failures+=not row['strict_timing_pass']
        (args.output/(name+'.receipt.json')).write_text(json.dumps(row,indent=2)+'\n')
        print(f"{name}: workflow={row['workflow_pass']} continuity={row['continuity_pass']} strict_timing={row['strict_timing_pass']}",flush=True)
    print(f'completed={len(cases)} strict_failures={failures}',flush=True)
    return int(failures!=0)


if __name__=='__main__':
    raise SystemExit(main())
