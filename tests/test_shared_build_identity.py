#!/usr/bin/env python3
"""Probe actual archive reuse and invalidation only in an explicit disposable copy."""
import argparse,json,os,subprocess,time
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--disposable-root',type=Path,required=True);a=p.parse_args();s=a.disposable_root.resolve()
if not str(s).startswith('/private/tmp/') or (s/'.git').exists():raise SystemExit('requires a disposable non-Git copy')
target='build/targets/macOS-arm64/shared/libcore_time.a';archive=s/target
results=[]
def run(name,extra=(),success=True,changed=None):
    before=archive.stat().st_mtime_ns if archive.exists() else None
    q=subprocess.run(['make',target,*extra],cwd=s,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
    after=archive.stat().st_mtime_ns if archive.exists() else None
    assert (q.returncode==0)==success,(name,q.stdout[-2000:])
    if changed is not None:assert (before!=after)==changed,(name,before,after)
    results.append({'case':name,'exit':q.returncode,'rebuilt':before!=after})
run('baseline');run('noop',changed=False)
time.sleep(1.1)
h=s/'third_party/codework_shared/core/core_time/include/core_time.h';os.utime(h,None)
run('header_timestamp',changed=True);run('header_noop',changed=False)
time.sleep(1.1)
f=s/'make/rules-build.mk';old=f.read_bytes();f.write_bytes(old+b'\n# disposable identity probe\n')
try:run('rule_bytes',changed=True)
finally:f.write_bytes(old)
run('rule_restore',changed=True)
run('compiler_command',['HOST_CC=clang'],changed=True)
run('compiler_restore',changed=True)
run('flags',['CFLAGS=-std=c11 -Wall -DL4_PROBE=1'],changed=True)
run('flags_restore',changed=True)
old=h.read_bytes();h.write_bytes(old+b'\n/* pin drift */\n')
try:run('pin_refusal',success=False,changed=False)
finally:h.write_bytes(old)
run('pin_retry',changed=True)
h.rename(h.with_suffix('.held'))
try:run('missing_header',success=False,changed=False)
finally:h.with_suffix('.held').rename(h)
run('missing_retry');run('final_noop',changed=False)
print(json.dumps(results,indent=2))
