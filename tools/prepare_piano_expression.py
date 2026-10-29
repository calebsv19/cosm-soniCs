"""Acquire pinned piano mechanics and reproduce bounded centered release layers."""
import pathlib,json,urllib.request,urllib.parse,hashlib,concurrent.futures,subprocess,wave,sys
import numpy as np
owner=pathlib.Path(__file__).resolve().parents[1];sys.path.insert(0,str(owner/'tools'));from sample_bank_pcm import decode_centered_pcm
bank=owner/'assets/instruments/salamander_piano_v1';d=bank/'expression';d.mkdir(exist_ok=True);original=d/'originals';original.mkdir(exist_ok=True);prepared=d/'prepared';prepared.mkdir(exist_ok=True)
base='https://raw.githubusercontent.com/sfzinstruments/SalamanderGrandPiano/3382bf9496bba2486f5ab0de55a264d1dfc38404/Samples/'
names=['rel%d.flac'%i for i in range(1,89)]+['pedalD1.flac','pedalD2.flac','pedalU1.flac','pedalU2.flac'];records=[];rows=[]
def download(name):
 url=base+urllib.parse.quote(name);p=original/name;data=p.read_bytes() if p.exists() else urllib.request.urlopen(url,timeout=60).read();p.write_bytes(data);return dict(file='originals/'+name,source=url,bytes=len(data),sha256=hashlib.sha256(data).hexdigest())
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:records=list(pool.map(download,names))
for r in records:
 source=d/r['file'];target=prepared/(source.stem+'.wav');subprocess.run(['/opt/homebrew/bin/ffmpeg','-v','error','-y','-i',str(source),'-ac','1','-ar','48000','-c:a','pcm_s24le',str(target)],check=True);rate,x=decode_centered_pcm(target);x=x[:48000];x[:min(144,len(x))]*=np.linspace(0,1,min(144,len(x)));x[-min(2400,len(x)):]*=np.linspace(1,0,min(2400,len(x)))
 ints=np.rint(np.clip(x,-1,1-1/8388608)*8388608).astype(np.int32);encoded=np.stack([ints&255,(ints>>8)&255,(ints>>16)&255],axis=1).astype(np.uint8)
 if len(encoded)%2:encoded=np.vstack([encoded,np.zeros((1,3),dtype=np.uint8)])
 with wave.open(str(target),'wb') as w:w.setnchannels(1);w.setsampwidth(3);w.setframerate(48000);w.writeframes(encoded.tobytes())
 r.update(prepared='prepared/'+target.name,prepared_sha256=hashlib.sha256(target.read_bytes()).hexdigest(),frames=len(encoded));rows.append('    {"expression/prepared/%s", {0}},'%target.name)
(d/'sample_map.inc').write_text('\n'.join(rows)+'\n');(d/'provenance.json').write_text(json.dumps(dict(library='Salamander Grand Piano v3',author='Alexander Holm',license='CC-BY-3.0',upstream_commit='3382bf9496bba2486f5ab0de55a264d1dfc38404',algorithm='centered-mono-48k-pcm24-first-1s-3ms-in-50ms-out-v1',files=records),indent=2)+'\n');print('Prepared',len(records),'release/pedal samples')
