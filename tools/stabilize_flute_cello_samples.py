"""Stabilize flute/cello layer timing and sustain loops without altering original recordings."""
import pathlib,json,wave,numpy as np,argparse
from sample_bank_pcm import decode_centered_pcm
p=argparse.ArgumentParser(description=__doc__);p.add_argument('bank_root',type=pathlib.Path);p.add_argument('--banks',nargs='+',choices=['vsco2_flute_v1','vsco2_cello_section_v1','vsco2_oboe_v1','vsco2_french_horn_v1','vsco2_bassoon_v1','vsco2_tenor_trombone_v1','vsco2_viola_section_v1','vsco2_violin_section_v1','vsco2_double_bass_v1'],default=['vsco2_flute_v1','vsco2_cello_section_v1']);args=p.parse_args();base=args.bank_root;rate=44100

def load(q):
 """Decode the centered original recording without changing its source asset."""
 source_rate,x=decode_centered_pcm(root/q['file']);assert source_rate==rate
 x=x[q['trim']:];ratio=q['pitch_ratio'];return np.interp(np.arange(int((len(x)-1)/ratio))*ratio,np.arange(len(x)),x)
def corr(a,b):
 """Measure normalized waveform correlation over a shared sustain window."""
 return float(np.mean(a*b)/np.sqrt(max(1e-20,float(np.mean(a*a)*np.mean(b*b)))))
def seams(x,start):
 """Evaluate each possible tail against one head with an offline FFT correlation."""
 head=x[start:start+cross];n=1<<(len(x)+cross-1).bit_length();dot=np.fft.irfft(np.fft.rfft(x,n)*np.conj(np.fft.rfft(head,n)),n)[:len(x)-cross+1]/cross;prefix=np.concatenate(([0.],np.cumsum(x*x)));power=(prefix[cross:]-prefix[:-cross])/cross;hp=float(np.mean(head*head));rho=dot/np.sqrt(np.maximum(1e-20,power*hp));boundary=.25*abs(np.log(np.maximum(1e-20,power)/max(1e-20,hp)));return rho,boundary

def stationarity(x,start,end):
 """Measure slow sustain-energy variation in 100 ms windows."""
 levels=[np.sqrt(np.mean(x[e:e+4410]**2)) for e in range(start,end-4410+1,4410)];return float(np.std(np.log(np.maximum(1e-10,levels))))
for name in args.banks:
 cross=5292 if name in ('vsco2_flute_v1','vsco2_oboe_v1','vsco2_bassoon_v1') else 10584
 root=base/name;baseline=json.loads((root/'preparation.json').read_text());rows=[]
 for note in sorted({q['note'] for q in baseline}):
  pair=sorted([q.copy() for q in baseline if q['note']==note],key=lambda q:q['layer']);a,b=[load(q) for q in pair];cycle=round(rate/(440*2**((note-69)/12)));best=None;seam_floor=.60 if name in ("vsco2_flute_v1","vsco2_oboe_v1","vsco2_bassoon_v1") else .15
  for start in range(35280,97020,8820):
   head=a[start:start+44100];offset=max(range(cycle+1),key=lambda d:corr(head,b[start+d:start+d+44100]));shifted=b[offset:];ar,al=seams(a,start);br,bl=seams(shifted,start);last=min(len(a),len(shifted))-30870-cross;first=start+35280
   end_heads=np.arange(first,last);valid=end_heads[(ar[first:last]>=seam_floor)&(br[first:last]>=seam_floor)]
   if not len(valid):continue
   scores=2-ar[valid]-br[valid]+al[valid]+bl[valid];chosen=valid[np.argsort(scores)[:80]]
   for tail in chosen:
    end=int(tail+cross);sa=stationarity(a,start,end);sb=stationarity(shifted,start,end);positions=np.linspace(start,end-4410,10,dtype=int);minimum=min(corr(a[t:t+4410],shifted[t:t+4410]) for t in positions);score=float(2-ar[tail]-br[tail]+al[tail]+bl[tail])+1.5*(sa+sb)+2*max(0,.5-minimum)
    if best is None or score<best[0]:best=(score,start,end,offset,float(ar[tail]),float(br[tail]),sa,sb)
  assert best is not None,('No qualified common seam',note)
  score,start,end,offset,ra,rb,sa,sb=best;positions=np.arange(13230,end-4410+1,1102)
  offsets=sorted(range(cycle+1),key=lambda d:min(corr(a[t:t+4410],b[t+d:t+d+4410]) for t in positions),reverse=True)
  for d in offsets:
   shifted=b[d:];rho=corr(shifted[start:start+cross],shifted[end-cross:end])
   if rho>=seam_floor:offset=d;rb=rho;break
  shifted=b[offset:];minimum=min(corr(a[t:t+4410],shifted[t:t+4410]) for t in np.linspace(start,end-4410,12,dtype=int));mean=corr(a[start:end],shifted[start:end])
  steady_correlations=[corr(a[t:t+4410],shifted[t:t+4410]) for t in range(start,end-4410+1,1102)]
  # Use the lower quartile so fluctuating correlation does not over-attenuate the quiet blend windows.
  compensation=max(0,min(1,float(np.percentile(steady_correlations,25))))
  pair[1]['trim']+=round(offset*pair[1]['pitch_ratio'])
  shifted=load(pair[1]);pair[1]['trim']-=round(offset*pair[1]['pitch_ratio'])
  rb=corr(shifted[start:start+cross],shifted[end-cross:end])
  compensation=max(0,min(1,float(np.percentile([corr(a[t:t+4410],shifted[t:t+4410]) for t in range(start,end-4410+1,1102)],25))))
  for layer,(q,x,rho) in enumerate(zip(pair,[a,shifted],[ra,rb])):
   q.update(trim=q['trim']+(round(offset*q['pitch_ratio']) if layer else 0),loop_start=start,loop_end=end,crossfade=cross,seam_correlation=max(0,min(1,rho)),sustain_log_rms_std=stationarity(x,start,end),rms=float(np.sqrt(np.mean(x[start:end]**2))),active_peak=float(max(abs(x[:end]))),loop_period_seconds=(end-start-cross)/rate,phase_alignment_frames=round(offset*q['pitch_ratio']) if layer else 0,minimum_pair_correlation=minimum,mean_pair_correlation=mean,blend_correlation=compensation);rows.append(q)
  target=min(.11,*[q['rms']*.6/max(q['active_peak'],.0001) for q in pair])
  for q in pair:q['gain']=target/max(q['rms'],.0001)
  print(note,round(minimum,3),round(min(ra,rb),3),flush=True)
 rows.sort(key=lambda q:q['file'])
 lines=['    {"%s", %d, %d, %d, %d, %d, %d, %.9ff, %.9ff, {0}},'%(q['file'],q['note'],q['layer'],q['trim'],q['loop_start'],q['loop_end'],q['crossfade'],q['seam_correlation'],q['gain']) for q in rows]
 (root/'sample_map_stabilized.inc').write_text('\n'.join(lines)+'\n');(root/'preparation_stabilized.json').write_text(json.dumps(rows,indent=2)+'\n');(root/'blend_correlation.inc').write_text(',\n'.join('%.9ff'%q['blend_correlation'] for q in rows)+'\n');print('Qualified common loops:',len(rows))
