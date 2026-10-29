"""Prepare sax-only aligned layers with coordinated waveform-matched sustain loops."""
import pathlib,json,wave,numpy as np,argparse
p=argparse.ArgumentParser(description=__doc__);p.add_argument('bank_root',type=pathlib.Path);root=p.parse_args().bank_root/'vcsl_tenor_saxophone_v1';baseline=json.loads((root/'preparation.json').read_text());rate=48000;cross=5760;rows=[]
def load(q):
 """Decode the centered original recording without changing its source asset."""
 with wave.open(str(root/q['file'])) as w:raw=w.readframes(w.getnframes());channels=w.getnchannels()
 b=np.frombuffer(raw,np.uint8).reshape(-1,3).astype(np.int32);v=b[:,0]|b[:,1]<<8|b[:,2]<<16;v=np.where(v&0x800000,v-0x1000000,v)/8388608.;return v.reshape(-1,channels).mean(1)[q['trim']:]
def corr(a,b):
 """Measure normalized waveform correlation over a shared sustain window."""
 return float(np.mean(a*b)/np.sqrt(max(1e-20,float(np.mean(a*a)*np.mean(b*b)))))
def seams(x,start):
 """Evaluate each possible tail against one head with an offline FFT correlation."""
 head=x[start:start+cross];n=1<<(len(x)+cross-1).bit_length();dot=np.fft.irfft(np.fft.rfft(x,n)*np.conj(np.fft.rfft(head,n)),n)[:len(x)-cross+1]/cross;prefix=np.concatenate(([0.],np.cumsum(x*x)));power=(prefix[cross:]-prefix[:-cross])/cross;hp=float(np.mean(head*head));rho=dot/np.sqrt(np.maximum(1e-20,power*hp));boundary=.25*abs(np.log(np.maximum(1e-20,power)/max(1e-20,hp)));return rho,boundary

def stationarity(x,start,end):
 """Measure slow sustain-energy variation in 100 ms windows."""
 levels=[np.sqrt(np.mean(x[e:e+4800]**2)) for e in range(start,end-4800+1,4800)];return float(np.std(np.log(np.maximum(1e-10,levels))))
for note in sorted({q['note'] for q in baseline}):
 pair=sorted([q.copy() for q in baseline if q['note']==note],key=lambda q:q['layer']);a,b=[load(q) for q in pair];cycle=round(rate/(440*2**((note-69)/12)));best=None;seam_floor=.65 if note in (78,88) else .85
 for start in range(38400,105600,9600):
  head=a[start:start+48000];offset=max(range(cycle+1),key=lambda d:corr(head,b[start+d:start+d+48000]));shifted=b[offset:];ar,al=seams(a,start);br,bl=seams(shifted,start);last=min(len(a),len(shifted))-33600-cross;first=start+(96000 if note<76 else 38400)
  end_heads=np.arange(first,last);valid=end_heads[(ar[first:last]>=seam_floor)&(br[first:last]>=seam_floor)]
  if not len(valid):continue
  scores=2-ar[valid]-br[valid]+al[valid]+bl[valid];chosen=valid[np.argsort(scores)[:80]]
  for tail in chosen:
   end=int(tail+cross);sa=stationarity(a,start,end);sb=stationarity(shifted,start,end);positions=np.linspace(start,end-4800,10,dtype=int);minimum=min(corr(a[t:t+4800],shifted[t:t+4800]) for t in positions);score=float(2-ar[tail]-br[tail]+al[tail]+bl[tail])+1.5*(sa+sb)+2*max(0,.5-minimum)
   if best is None or score<best[0]:best=(score,start,end,offset,float(ar[tail]),float(br[tail]),sa,sb)
 assert best is not None,('No qualified common seam',note)
 score,start,end,offset,ra,rb,sa,sb=best;positions=np.arange(14400,end-4800+1,1200) if note>=76 else np.linspace(14400,end-4800,12,dtype=int)
 offsets=sorted(range(cycle+1),key=lambda d:min(corr(a[t:t+4800],b[t+d:t+d+4800]) for t in positions),reverse=True)
 for d in offsets:
  shifted=b[d:];rho=corr(shifted[start:start+cross],shifted[end-cross:end])
  if rho>=seam_floor:offset=d;rb=rho;break
 shifted=b[offset:];minimum=min(corr(a[t:t+4800],shifted[t:t+4800]) for t in np.linspace(start,end-4800,12,dtype=int));mean=corr(a[start:end],shifted[start:end])
 steady_correlations=[corr(a[t:t+4800],shifted[t:t+4800]) for t in range(start,end-4800+1,1200)]
 # Use the lower quartile so fluctuating correlation does not over-attenuate the quiet blend windows.
 compensation=max(0,min(1,float(np.percentile(steady_correlations,25))))
 for layer,(q,x,rho) in enumerate(zip(pair,[a,shifted],[ra,rb])):
  q.update(trim=q['trim']+(offset if layer else 0),loop_start=start,loop_end=end,crossfade=cross,seam_correlation=max(0,min(1,rho)),sustain_log_rms_std=stationarity(x,start,end),rms=float(np.sqrt(np.mean(x[start:end]**2))),active_peak=float(max(abs(x[:end]))),loop_period_seconds=(end-start-cross)/rate,phase_alignment_frames=offset if layer else 0,minimum_pair_correlation=minimum,mean_pair_correlation=mean,blend_correlation=compensation);rows.append(q)
 target=min(.11,*[q['rms']*.6/max(q['active_peak'],.0001) for q in pair])
 for q in pair:q['gain']=target/max(q['rms'],.0001)
 print(note,round(minimum,3),round(min(ra,rb),3),flush=True)
rows.sort(key=lambda q:q['file'])
lines=['    {"%s", %d, %d, %d, %d, %d, %d, %.9ff, %.9ff, {0}},'%(q['file'],q['note'],q['layer'],q['trim'],q['loop_start'],q['loop_end'],q['crossfade'],q['seam_correlation'],q['gain']) for q in rows]
(root/'sample_map_stabilized.inc').write_text('\n'.join(lines)+'\n');(root/'preparation_stabilized.json').write_text(json.dumps(rows,indent=2)+'\n');(root/'blend_correlation.inc').write_text(',\n'.join('%.9ff'%q['blend_correlation'] for q in rows)+'\n');print('Qualified common loops:',len(rows))
