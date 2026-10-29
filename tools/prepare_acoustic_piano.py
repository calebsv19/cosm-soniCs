"""Prepare attributed three-layer acoustic piano strikes with natural decay and damped endpoints."""
import argparse,pathlib,subprocess,wave,json,hashlib,re,numpy as np
from sample_bank_pcm import decode_centered_pcm
p=argparse.ArgumentParser(description=__doc__);p.add_argument('bank_root',type=pathlib.Path);args=p.parse_args();root=args.bank_root/'salamander_piano_v1';prepared=root/'prepared';prepared.mkdir(exist_ok=True);rows=[];records=[]
for source in sorted((root/'originals').glob('*.flac')):
    match=re.fullmatch(r'([A-G])(#?)(\d)v(1|8|16)',source.stem);assert match,source
    note={'C':0,'D':2,'E':4,'F':5,'G':7,'A':9,'B':11}[match[1]]+bool(match[2])+12*(int(match[3])+1);layer={'1':0,'8':1,'16':2}[match[4]]
    decoded=prepared/(source.stem+'.decode.wav');subprocess.run(['/opt/homebrew/bin/ffmpeg','-v','error','-y','-i',str(source),'-ac','1','-ar','48000','-c:a','pcm_s24le',str(decoded)],check=True)
    rate,x=decode_centered_pcm(decoded);assert rate==48000;peak=float(max(abs(x)));hot=np.flatnonzero(abs(x)>max(.00002,peak*.001));trim=max(0,int(hot[0])-240);x=x[trim:];fade=min(2400,len(x));x[-fade:]*=np.linspace(1,0,fade);target=prepared/(source.stem+'.wav')
    integers=np.rint(np.clip(x,-1,1-1/8388608)*8388608).astype(np.int32);encoded=np.stack([integers&255,(integers>>8)&255,(integers>>16)&255],axis=1).astype(np.uint8)
    # Keep RIFF data even-sized by appending a silent frame when the mono frame count is odd.
    if len(encoded)%2:encoded=np.vstack([encoded,np.zeros((1,3),dtype=np.uint8)])
    with wave.open(str(target),'wb') as w:w.setnchannels(1);w.setsampwidth(3);w.setframerate(48000);w.writeframes(encoded.tobytes())
    decoded.unlink();rows.append(dict(file='prepared/'+target.name,note=note,layer=layer,trim=0,loop_start=0,loop_end=0,crossfade=0,seam_correlation=1.,peak=peak,seconds=len(encoded)/48000))
    records.append(dict(file='prepared/'+target.name,source='originals/'+source.name,source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),sha256=hashlib.sha256(target.read_bytes()).hexdigest(),bytes=target.stat().st_size,source_trim_frames=trim,end_fade_frames=fade))
gain=.32/max(q['peak'] for q in rows)
for q in rows:q['gain']=gain
(root/'sample_map.inc').write_text('\n'.join('    {"%s", %d, %d, 0, 0, 0, 0, 1.f, %.9ff, {0}},'%(q['file'],q['note'],q['layer'],gain) for q in rows)+'\n');(root/'preparation.json').write_text(json.dumps(rows,indent=2)+'\n');(root/'prepared/provenance.json').write_text(json.dumps(dict(algorithm='centered-mono-natural-decay-v1',files=records),indent=2)+'\n');print('Piano strikes:',len(rows),'range:',min(q['note'] for q in rows),max(q['note'] for q in rows),'bank gain:',gain)
