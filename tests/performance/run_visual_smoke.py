#!/usr/bin/env python3
"""Captures the current application in an isolated synthetic project and dummy-audio environment."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import struct
import zlib
import binascii


def png_preview(source):
    """Losslessly converts the renderer's known opaque BGRA BMP into a portable RGB PNG preview."""
    data=source.read_bytes();width,height=struct.unpack_from('<ii',data,18)
    offset=struct.unpack_from('<I',data,10)[0]
    if struct.unpack_from('<IIII',data,54)!=(0xff0000,0xff00,0xff,0xff000000) or height<=0:
        raise ValueError('Unexpected renderer BMP masks/orientation')
    rows=[]
    for y in range(height):
        row=data[offset+(height-1-y)*width*4:offset+(height-y)*width*4]
        rgb=bytearray(width*3);rgb[0::3]=row[2::4];rgb[1::3]=row[1::4];rgb[2::3]=row[0::4]
        rows.append(b'\0'+rgb)
    def chunk(tag,payload):
        """Encodes one PNG chunk with its required checksum."""
        return struct.pack('>I',len(payload))+tag+payload+struct.pack('>I',binascii.crc32(tag+payload)&0xffffffff)
    source.with_suffix('.png').write_bytes(b'\x89PNG\r\n\x1a\n'+
        chunk(b'IHDR',struct.pack('>IIBBBBB',width,height,8,2,0,0,0))+
        chunk(b'IDAT',zlib.compress(b''.join(rows)))+chunk(b'IEND',b''))


def main():
    """Retains startup/capture evidence without reading or autosaving the user's active project."""
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--project',type=Path,help='Optional synthetic fixture project to copy')
    args=parser.parse_args();root=Path(__file__).resolve().parents[2];binary=args.binary.resolve()
    output=args.output.resolve();output.mkdir(parents=True,exist_ok=False)
    working=output/'isolated';(working/'config/templates').mkdir(parents=True)
    (working/'config/runtime').mkdir();(working/'audio').mkdir()
    (working/'assets').symlink_to(root/'assets',target_is_directory=True)
    (working/'include').mkdir();(working/'include/fonts').symlink_to(root/'include/fonts',target_is_directory=True)
    shutil.copyfile(root/'config/engine.cfg',working/'config/engine.cfg')
    source=args.project or root/'config/templates/public_default_project.json'
    project=json.loads(source.read_text())
    project['transport_playing']=False
    project['data_paths']={'input_root':str(working/'audio'),'output_root':str(working/'config'),
                           'library_copy_root':str(working/'audio')}
    project.setdefault('library',{})['directory']=str(working/'audio')
    (working/'config/templates/public_default_project.json').write_text(json.dumps(project,indent=2)+'\n')
    (working/'config/runtime/data_paths.cfg').write_text(''.join(f'{k}={v}\n' for k,v in project['data_paths'].items()))
    env={**os.environ,'SDL_AUDIODRIVER':'dummy','DAW_VISUAL_ARTIFACT_ONCE':'1',
         'DAW_VISUAL_ARTIFACT_PATH':str(output/'first-frame.bmp')}
    result={'binary':str(binary),'binary_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),
            'project_source':str(source),'project_source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),
            'physical_audio':False,'interactive_acceptance':False}
    try:
        with (output/'stdout.log').open('w') as stdout,(output/'stderr.log').open('w') as stderr:
            process=subprocess.run([str(binary)],cwd=working,env=env,stdout=stdout,stderr=stderr,timeout=60)
        result['exit_code']=process.returncode
        spec=importlib.util.spec_from_file_location('daw_visual_verify',root/'tools/verify-vulkan-rollout.py')
        module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        width,height,colors=module.bmp_evidence(output/'first-frame.bmp')
        fonts_loaded='Failed to load font' not in (output/'stderr.log').read_text()
        png_preview(output/'first-frame.bmp')
        result.update(width=width,height=height,sampled_colors=colors,fonts_loaded=fonts_loaded,passed=process.returncode==0 and fonts_loaded)
    except (subprocess.TimeoutExpired,OSError,SystemExit,ValueError) as exc:
        result.update(passed=False,error=str(exc))
    (output/'receipt.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result),flush=True)
    return int(not result['passed'])


if __name__=='__main__':
    raise SystemExit(main())
