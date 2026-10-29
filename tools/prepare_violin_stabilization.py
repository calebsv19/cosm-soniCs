"""Prepare longer phase-matched sustain loops without changing original WAV bytes."""
import json,pathlib,wave,re,argparse
import numpy as np
parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('sample_root',type=pathlib.Path);root=parser.parse_args().sample_root
original=json.loads((root/'preparation.json').read_text());rows=[]
for record in original:
 f=root/record['file']
 with wave.open(str(f)) as w:
  rate=w.getframerate();channels=w.getnchannels();width=w.getsampwidth();raw=w.readframes(w.getnframes())
 if width==3:
  b=np.frombuffer(raw,np.uint8).reshape(-1,3).astype(np.int32);v=b[:,0]|(b[:,1]<<8)|(b[:,2]<<16);v=np.where(v&0x800000,v-0x1000000,v)/8388608.
 else:v=np.frombuffer(raw,'<i2')/32768.
 x=v.reshape(-1,channels).mean(axis=1)[record['trim']:];cross=5292
 best=None
 # Favor a smooth amplitude match and positive waveform correlation over at least 1.8 seconds.
 for start in range(int(rate*.9),int(rate*2.5),int(rate*.2)):
  head=x[start:start+cross];a=float(np.mean(head*head))
  for end in range(start+int(rate*2),min(len(x)-int(rate*.7),int(rate*8)),int(rate*.1)):
   tail=x[end-cross:end];b=float(np.mean(tail*tail));rho=float(np.mean(head*tail)/np.sqrt(max(1e-20,a*b)))
   level=abs(np.log(max(1e-12,b)/max(1e-12,a)))
   score=1-rho+.25*level
   if best is None or score<best[0]:best=(score,start,end)
 score,start,end=best;chosen=end;head=x[start:start+cross];a=float(np.mean(head*head));period=int(round(rate/record['expected_hz']))
 for offset in range(-period,period+1):
  e=end+offset;tail=x[e-cross:e];b=float(np.mean(tail*tail));rho=float(np.mean(head*tail)/np.sqrt(max(1e-20,a*b)));s=1-rho+.25*abs(np.log(max(1e-12,b)/max(1e-12,a)))
  if s<score:score=s;chosen=e
 end=chosen
 tail=x[end-cross:end];head=x[start:start+cross];rho=float(np.mean(head*tail)/np.sqrt(max(1e-20,float(np.mean(head*head)*np.mean(tail*tail)))))
 segment=x[start:end];rms=float(np.sqrt(np.mean(segment*segment)))
 m=re.search(r'_([A-G])(\d)_([pf])',record['file']);note={'C':0,'D':2,'E':4,'F':5,'G':7,'A':9,'B':11}[m[1]]+12*(int(m[2])+1)
 row=dict(file=record['file'],note=note,strong=m[3]=='f',trim=record['trim'],loop_start=start,loop_end=end,crossfade=cross,seam_correlation=max(0,min(1,rho)),rms=rms,active_peak=float(np.max(np.abs(x[:end]))),seam_score=score,loop_period_seconds=(end-start-cross)/rate)
 rows.append(row)
# Match each recorded layer's steady loudness; the engine supplies the intended expressive loudness curve.
for note in sorted(set(q['note'] for q in rows)):
 pair=[q for q in rows if q['note']==note];target=min(.11,*[q['rms']*.6/max(q['active_peak'],.0001) for q in pair])
 for row in pair:row['gain']=target/max(row['rms'],.0001)
lines=['    {"%s", %d, %d, %d, %d, %d, %d, %.9ff, %.9ff, {0}},'%(q['file'],q['note'],q['strong'],q['trim'],q['loop_start'],q['loop_end'],q['crossfade'],q['seam_correlation'],q['gain']) for q in rows]
(root/'sample_map_stabilized.inc').write_text('\n'.join(lines)+'\n');(root/'preparation_stabilized.json').write_text(json.dumps(rows,indent=2)+'\n')
print(json.dumps({'min_period':min(q['loop_period_seconds'] for q in rows),'max_period':max(q['loop_period_seconds'] for q in rows),'max_seam_score':max(q['seam_score'] for q in rows),'max_gain':max(q['gain'] for q in rows),'A4':[q for q in rows if '_A4_' in q['file']]},indent=2))
