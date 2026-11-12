#!/usr/bin/env python3
"""Check exact selected module bytes without importing or deleting output."""
import argparse
import hashlib
import json
from pathlib import Path


def verify(manifest, roots):
    errors = []
    if set(roots) != set(manifest['modules']):
        return ['selected module set differs from manifest']
    for name, expected in manifest['files'].items():
        module = next((m for m in roots if name.startswith(m+'/')), None)
        if module is None:
            errors.append('unowned manifest input: '+name)
            continue
        path = roots[module]/name[len(module)+1:]
        if path.is_symlink() or not path.is_file():
            errors.append('missing/nonordinary: '+name)
        elif hashlib.sha256(path.read_bytes()).hexdigest() != expected:
            errors.append('changed: '+name)
    for module, root in roots.items():
        for folder in ['src', 'include', 'shaders']:
            for path in (root/folder).rglob('*'):
                if path.is_file() and path.suffix in {'.c','.h','.inc','.cpp','.hpp','.m','.mm','.vert','.frag','.comp','.glsl','.spv'}:
                    name = module+'/'+str(path.relative_to(root))
                    if name not in manifest['files']:
                        errors.append('unrecorded source: '+name)
    return errors


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--manifest', type=Path, required=True)
    p.add_argument('--module', action='append', default=[])
    a = p.parse_args()
    roots = {}
    for item in a.module:
        name, path = item.split('=', 1)
        if name in roots: p.error('duplicate module: '+name)
        roots[name] = Path(path).resolve()
    manifest = json.loads(a.manifest.read_text())
    errors = verify(manifest, roots)
    if errors: raise SystemExit('\n'.join(errors))
    print('Verified',len(manifest['files']),'files in',len(roots),'selected modules')


if __name__ == '__main__': main()
