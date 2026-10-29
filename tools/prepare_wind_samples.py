"""Prepare pinned sustain banks with measured tuning, long loops and matched layers."""
import pathlib,wave,json,re,argparse,numpy as np
parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('bank_root',type=pathlib.Path);parser.add_argument('--stabilize',action='store_true');args=parser.parse_args();base=args.bank_root
for name in ['vsco2_trumpet_v1','vsco2_clarinet_v1']:
 root=base/name;rows=[]
 for f in sorted(root.glob('*.wav')):
  with wave.open(str(f)) as w:rate=w.getframerate();channels=w.getnchannels();width=w.getsampwidth();raw=w.readframes(w.getnframes())
  if width==3:
   b=np.frombuffer(raw,np.uint8).reshape(-1,3).astype(np.int32);v=b[:,0]|(b[:,1]<<8)|(b[:,2]<<16);v=np.where(v&0x800000,v-0x1000000,v)/8388608.
  else:v=np.frombuffer(raw,'<i2')/32768.
  x=v.reshape(-1,channels).mean(axis=1);peak=float(max(abs(x)));hot=np.flatnonzero(abs(x)>max(.0003,peak*.008));trim=max(0,int(hot[0])-441);x=x[trim:]
  match=re.search(r'_([A-G])(#?)(\d)_v(\d)',f.name);pitch={'C':0,'D':2,'E':4,'F':5,'G':7,'A':9,'B':11}[match[1]]+bool(match[2])+12*(int(match[3])+2)
  correction=-1 if name=='vsco2_clarinet_v1' and match[1]=='F' and match[2]=='#' and match[3]=='5' else 0
  pitch+=correction
  layer=({'1':0,'3':1} if 'trumpet' in name else {'1':0,'2':1,'3':2})[match[4]]
  # Both source producers label octaves one below soniCs C4=60; verify harmonic families at sounding pitch.
  expected=440*2**((pitch-69)/12);segment=x[rate:2*rate];N=131072;spec=abs(np.fft.rfft(segment*np.hanning(len(segment)),N));freq=np.fft.rfftfreq(N,1/rate)
  candidate=np.linspace(expected*2**(-50/1200),expected*2**(50/1200),401);scores=sum(np.interp(candidate*k,freq,spec)/k for k in range(1,6));measured=float(candidate[np.argmax(scores)])
  assert max(spec[(freq>expected*.96)&(freq<expected*1.04)])>1,(f.name,'missing expected fundamental')
  cross=5292;best=None
  # Score 100 ms sustain energy as well as the overlap so repeated swells do not dominate the loop.
  power_prefix=np.concatenate(([0.],np.cumsum(x*x)))
  def loop_score(start,end,rho,a,b):
   """Measure seam agreement, boundary level matching and steady sustain energy."""
   edges=np.arange(start,end-4410+1,4410)
   levels=np.sqrt(np.maximum(1e-20,(power_prefix[edges+4410]-power_prefix[edges])/4410))
   stationarity=float(np.std(np.log(levels)))
   score=1-rho+.25*abs(np.log(max(1e-12,b)/max(1e-12,a)))
   return score+(1.5*stationarity if args.stabilize else 0),stationarity
  for start in range(int(rate*.8),int(rate*2.2),8820):
   head=x[start:start+cross];a=float(np.mean(head*head))
   for end in range(start+int(rate*2),min(len(x)-int(rate*.7),int(rate*8)),4410):
    tail=x[end-cross:end];b=float(np.mean(tail*tail));rho=float(np.mean(head*tail)/np.sqrt(max(1e-20,a*b)));score,_=loop_score(start,end,rho,a,b)
    if best is None or score<best[0]:best=(score,start,end)
  assert best is not None,f.name
  score,start,end=best;chosen=end;head=x[start:start+cross];a=float(np.mean(head*head));period=round(rate/expected)
  for offset in range(-period,period+1):
   e=end+offset;tail=x[e-cross:e];b=float(np.mean(tail*tail));rho=float(np.mean(head*tail)/np.sqrt(max(1e-20,a*b)));s,_=loop_score(start,e,rho,a,b)
   if s<score:score=s;chosen=e
  end=chosen;tail=x[end-cross:end];rho=float(np.mean(head*tail)/np.sqrt(max(1e-20,float(np.mean(head*head)*np.mean(tail*tail)))))
  _,stationarity=loop_score(start,end,rho,float(np.mean(head*head)),float(np.mean(tail*tail)))
  rows.append(dict(sustain_log_rms_std=stationarity,file=f.name,note=int(pitch),layer=layer,trim=trim,loop_start=start,loop_end=end,crossfade=cross,seam_correlation=max(0,min(1,rho)),rms=float(np.sqrt(np.mean(x[start:end]**2))),active_peak=float(max(abs(x[:end]))),loop_period_seconds=(end-start-cross)/rate,measured_hz=measured,expected_hz=expected,cents=float(1200*np.log2(measured/expected)),source_octave_adjustment=12,source_semitone_correction=correction,seconds=len(x)/rate))
 for note in sorted(set(q['note'] for q in rows)):
  pair=[q for q in rows if q['note']==note];target=min(.11,*[q['rms']*.6/max(q['active_peak'],.0001) for q in pair])
  for q in pair:q['gain']=target/max(q['rms'],.0001)
 lines=['    {"%s", %d, %d, %d, %d, %d, %d, %.9ff, %.9ff, {0}},'%(q['file'],q['note'],q['layer'],q['trim'],q['loop_start'],q['loop_end'],q['crossfade'],q['seam_correlation'],q['gain']) for q in rows]
 suffix='_stabilized' if args.stabilize else ''
 (root/('sample_map'+suffix+'.inc')).write_text('\n'.join(lines)+'\n');(root/('preparation'+suffix+'.json')).write_text(json.dumps(rows,indent=2)+'\n')
 print(json.dumps(dict(bank=name,count=len(rows),minimum_note=min(q['note'] for q in rows),maximum_note=max(q['note'] for q in rows),layers=1+max(q['layer'] for q in rows),min_loop=min(q['loop_period_seconds'] for q in rows),max_pitch_deviation=max(abs(q['cents']) for q in rows)),indent=2))
