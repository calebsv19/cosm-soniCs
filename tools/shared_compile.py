#!/usr/bin/env python3
"""Record actual host-compiler inputs as absolute dependencies of the consumer archive."""
import argparse
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys


def escape(path):
    value = str(path)
    if any(c in value for c in '\n\r\0:%|;*?[]\\'):
        raise ValueError('unsupported dependency path: '+value)
    return value.replace('$','$$').replace('#','\\#').replace(' ','\\ ').replace('\t','\\\t')


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--target',required=True)
    p.add_argument('command',nargs=argparse.REMAINDER)
    a=p.parse_args();command=a.command[1:] if a.command[:1]==['--'] else a.command
    if '-c' not in command: return subprocess.call(command)
    # Module makefiles may already request -MMD. Replace that request rather
    # than combine conflicting dependency modes under their -Werror policy.
    filtered=[];skip=False
    for item in command:
        if skip: skip=False;continue
        if item in {'-MF','-MT','-MQ'}: skip=True;continue
        if item in {'-MD','-MMD','-MP'}: continue
        filtered.append(item)
    command=filtered
    output=Path(command[command.index('-o')+1]);dep=Path(str(output)+'.l4.d');raw=Path(str(dep)+'.raw')
    result=subprocess.call(command+['-MD','-MP','-MF',str(raw),'-MT',a.target])
    if result:
        raw.unlink(missing_ok=True)
        return result
    try:
        line=raw.read_text().replace('\\\n',' ').splitlines()[0]
        dependencies=sorted({str(Path(n).resolve()) for n in shlex.split(line.split(':',1)[1])})
        if not dependencies or not all(Path(n).is_file() for n in dependencies):
            raise ValueError('missing compiled dependency')
        text=escape(a.target)+': '+' '.join(escape(n) for n in dependencies)+'\n'
        text+=''.join(escape(n)+':\n' for n in dependencies)
        temporary=Path(str(dep)+'.tmp');temporary.write_text(text);os.replace(temporary,dep)
        Path(str(output)+'.l4.json').write_text(json.dumps({'cwd':str(Path.cwd()),'command':command,'archive':a.target,'dependencies':dependencies},indent=2)+'\n')
    finally:
        raw.unlink(missing_ok=True)
    return 0


if __name__=='__main__':
    try: raise SystemExit(main())
    except (OSError,ValueError,IndexError) as error:
        print('shared dependency capture failed: '+str(error),file=sys.stderr)
        raise SystemExit(1)
