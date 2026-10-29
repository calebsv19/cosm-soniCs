"""Prepare bounded time-varying layer-energy correction and improve qualified cello overlaps."""
import argparse,json,pathlib,wave,numpy as np,math
from sample_bank_pcm import decode_centered_pcm
parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('bank_root',type=pathlib.Path);parser.add_argument('--banks',nargs='+',choices=['vsco2_flute_v1','vsco2_cello_section_v1','vsco2_oboe_v1','vsco2_french_horn_v1','vsco2_bassoon_v1','vsco2_tenor_trombone_v1','vsco2_viola_section_v1','vsco2_violin_section_v1','vsco2_double_bass_v1'],default=['vsco2_flute_v1','vsco2_cello_section_v1']);parser.add_argument('--refine-new-winds',action='store_true',help='Qualify oboe/horn loop refinements without changing other bank defaults');args=parser.parse_args();rate=44100

def load(root,q):
    """Decode original centered audio and evaluate the bank's fixed pitch correction."""
    source_rate,x=decode_centered_pcm(root/q['file']);assert source_rate==rate
    x=x[q['trim']:]
    return np.interp(np.arange(int((len(x)-1)/q['pitch_ratio']))*q['pitch_ratio'],np.arange(len(x)),x)

def seams(x,start,cross):
    """Measure candidate overlap correlation and RMS mismatch with one FFT."""
    h=x[start:start+cross];n=1<<(len(x)+cross-1).bit_length()
    dot=np.fft.irfft(np.fft.rfft(x,n)*np.conj(np.fft.rfft(h,n)),n)[:len(x)-cross+1]/cross
    prefix=np.r_[0.,np.cumsum(x*x)];power=(prefix[cross:]-prefix[:-cross])/cross;hp=float(np.mean(h*h))
    return dot/np.sqrt(np.maximum(1e-20,power*hp)),abs(np.log(np.maximum(1e-20,power)/max(1e-20,hp)))

def stationarity(x,start,end):
    """Measure natural sustain level movement in 100 ms windows."""
    values=[np.sqrt(np.mean(x[t:t+4410]**2)) for t in range(start,end-4410+1,4410)]
    return float(np.std(np.log(np.maximum(1e-10,values))))

def improve_loop(pair,signals,profile="cello"):
    """Retain or replace a cello loop only when seam and stationarity both qualify."""
    start=pair[0]['loop_start'];end=pair[0]['loop_end'];cross=pair[0]['crossfade'];old_min=min(q['seam_correlation'] for q in pair)
    best=None
    for width in ([cross,14112] if profile=="horn" else ([cross] if profile=="oboe" else [cross,14112,17640])):
        for head in (range(35280,110251,4410) if profile=="horn" else range(max(35280,start-4410),start+4411,2205)):
            ar,al=seams(signals[0],head,width);br,bl=seams(signals[1],head,width)
            first=max(head+width+35280,end-width-(44100 if profile=="horn" else 11025));last=min(len(ar),len(br),end-width+(44101 if profile=="horn" else 11026))
            if first>=last:continue
            indices=np.arange(first,last);score=2-ar[indices]-br[indices]+.7*(1-np.minimum(ar[indices],br[indices]))+.4*(al[indices]+bl[indices])
            for k in np.argsort(score)[:3]:
                tail=int(indices[k]);finish=tail+width;ra=float(ar[tail]);rb=float(br[tail]);sa=stationarity(signals[0],head,finish);sb=stationarity(signals[1],head,finish)
                if min(ra,rb)<old_min+.03 or sa>pair[0]['sustain_log_rms_std']+.035 or sb>pair[1]['sustain_log_rms_std']+.035:continue
                rank=float(score[k])+1.5*(sa+sb)
                if best is None or rank<best[0]:best=(rank,head,finish,width,ra,rb,sa,sb)
    if best:
        _,start,end,cross,ra,rb,sa,sb=best
        for q,rho,std in zip(pair,[ra,rb],[sa,sb]):q.update(loop_start=start,loop_end=end,crossfade=cross,seam_correlation=max(0,min(1,rho)),sustain_log_rms_std=std,loop_period_seconds=(end-start-cross)/rate)
    return bool(best)

def looped(x,q,position):
    """Evaluate production overlap geometry, level and seam-energy compensation offline."""
    p=np.asarray(position,dtype=float).copy();end=q['loop_end'];start=q['loop_start'];cross=q['crossfade'];period=end-start-cross
    wrapped=p>=end;p[wrapped]=start+cross+np.mod(p[wrapped]-end,period)
    value=np.interp(p,np.arange(len(x)),x,left=0,right=0);overlap=p>=end-cross;u=p[overlap]-(end-cross);w=.5-.5*np.cos(np.pi*u/cross)
    power=1-2*w*(1-w)*(1-q['seam_correlation']);value[overlap]=(value[overlap]*(1-w)+np.interp(start+u,np.arange(len(x)),x)*w)/np.sqrt(power)
    value[position<0]=0
    return value*q['gain']

def moments(signals,pair,centers):
    """Measure centered 100 ms soft/strong energy and covariance at requested positions."""
    half=2205;first=math.floor(float(min(centers)))-half;last=math.ceil(float(max(centers)))+half+2
    positions=np.arange(first,last,dtype=float);a,b=[looped(x,q,positions) for x,q in zip(signals,pair)];result=[]
    for values in [a*a,b*b,a*b]:
        prefix=np.r_[0.,np.cumsum(values)];coords=np.arange(len(prefix));lo=centers-half-first;hi=centers+half-first
        result.append((np.interp(hi,coords,prefix)-np.interp(lo,coords,prefix))/(2*half))
    return np.stack(result,axis=1)

def energy_model(signals,pair):
    """Prepare uniform onset and periodic energy lookup points with matching wrap endpoints."""
    end=pair[0]['loop_end'];period=end-pair[0]['loop_start']-pair[0]['crossfade'];initial_count=math.ceil(end/441)+1;cycle_count=math.ceil(period/441)
    initial=moments(signals,pair,np.linspace(0,end,initial_count));cycle=moments(signals,pair,end+period+np.arange(cycle_count)*period/cycle_count);initial[-1]=cycle[0]
    return dict(note=pair[0]['note'],loop_end=end,period=period,initial=initial.tolist(),cycle=cycle.tolist())

def blend_limits(signals,pair,model):
    """Qualify loop edits against bounded corrected mixing in 100 ms sustain windows."""
    pos=np.arange(rate*16,dtype=float);x,y=[looped(z,q,pos) for z,q in zip(signals,pair)];initial=np.array(model['initial']);cycle=np.array(model['cycle']);end=model['loop_end'];period=model['period'];fields=[]
    for k in range(3):
        values=np.interp(pos,np.linspace(0,end,len(initial)),initial[:,k]);repeat=pos>=end
        values[repeat]=np.interp(np.mod(pos[repeat]-end,period),np.linspace(0,period,len(cycle)+1),np.r_[cycle[:,k],cycle[0,k]]);fields.append(values)
    minimum=2.;deviation=0.
    for dynamics in [.25,.5,.75]:
        t=dynamics**3*(dynamics*(6*dynamics-15)+10);c=np.cos(t*np.pi/2);s=np.sin(t*np.pi/2);expected=c*c*fields[0]+s*s*fields[1];gain=np.clip(np.sqrt(expected/np.maximum(expected+2*c*s*fields[2],1e-20)),.75,1.35);mix=(c*x+s*y)*gain
        n=15*rate//4410;xw=x[rate:rate+n*4410].reshape(n,4410);yw=y[rate:rate+n*4410].reshape(n,4410);mw=mix[rate:rate+n*4410].reshape(n,4410);ratio=np.sqrt(np.mean(mw*mw,axis=1)/np.maximum(np.mean(c*c*xw*xw+s*s*yw*yw,axis=1),1e-20));minimum=min(minimum,float(min(ratio)));deviation=max(deviation,float(max(abs(ratio-1))))
    return minimum,deviation

for bank in args.banks:
    prefix={'vsco2_flute_v1':'flute','vsco2_cello_section_v1':'cello','vsco2_oboe_v1':'oboe','vsco2_french_horn_v1':'horn','vsco2_bassoon_v1':'bassoon','vsco2_tenor_trombone_v1':'trombone','vsco2_viola_section_v1':'viola','vsco2_violin_section_v1':'vlnsection','vsco2_double_bass_v1':'doublebass'}[bank]
    root=args.bank_root/bank;baseline=json.loads((root/'preparation_stabilized.json').read_text());rows=[];curves=[];changes=[]
    for note in sorted({q['note'] for q in baseline}):
        pair=sorted([q.copy() for q in baseline if q['note']==note],key=lambda q:q['layer']);signals=[load(root,q) for q in pair]
        changed=improve_loop(pair,signals,{'bassoon':'oboe','trombone':'horn'}.get(prefix,prefix)) if prefix=='cello' or args.refine_new_winds and prefix in ('oboe','horn','bassoon','trombone') else False
        # Keep recorded layer gains and pitch centers unchanged; this pass targets overlap and mixing.
        for q in pair:q['rms']=float(np.sqrt(np.mean(signals[q['layer']][q['loop_start']:q['loop_end']]**2)))
        model=energy_model(signals,pair)
        if changed:
            original=sorted([q.copy() for q in baseline if q['note']==note],key=lambda q:q['layer']);previous=energy_model(signals,original);old_min,old_deviation=blend_limits(signals,original,previous);new_min,new_deviation=blend_limits(signals,pair,model)
            if new_min<old_min-.015 or new_deviation>old_deviation+.02:
                pair=original;model=previous;changed=False
        curves.append(model)
        rows.extend(pair);changes.append(dict(note=note,loop_changed=changed,before_min_seam=min(q['seam_correlation'] for q in baseline if q['note']==note),after_min_seam=min(q['seam_correlation'] for q in pair),crossfade_ms=pair[0]['crossfade']*1000/rate))
        print(bank,note,'loop improved' if changed else 'loop retained',round(changes[-1]['after_min_seam'],3),flush=True)
    rows.sort(key=lambda q:q['file']);lookup={q['note']:i for i,q in enumerate(curves)}
    generated=[]
    for i,q in enumerate(curves):
        for name in ['initial','cycle']:
            generated.append(f'static const InstrumentSampleBlendPoint {prefix}_blend_{i}_{name}[] = {{')
            generated.extend('    {%.9ef, %.9ef, %.9ef},'%tuple(point) for point in q[name]);generated.append('};')
    generated.append(f'static const InstrumentSampleBlendEnvelope {prefix}_blend_envelopes[] = {{')
    for q in rows:
        i=lookup[q['note']];c=curves[i];generated.append(f'    {{{prefix}_blend_{i}_initial, {len(c["initial"])}, {prefix}_blend_{i}_cycle, {len(c["cycle"])}, {c["loop_end"]}, {c["period"]}}},')
    generated.append('};')
    (root/'blend_envelopes.inc').write_text('\n'.join(generated)+'\n')
    (root/'blend_envelopes.json').write_text(json.dumps(dict(window_frames=4410,step_maximum_frames=441,minimum_gain=.75,maximum_gain=1.35,curves=curves,loop_changes=changes),indent=2)+'\n')
    lines=['    {"%s", %d, %d, %d, %d, %d, %d, %.9ff, %.9ff, {0}},'%(q['file'],q['note'],q['layer'],q['trim'],q['loop_start'],q['loop_end'],q['crossfade'],q['seam_correlation'],q['gain']) for q in rows]
    (root/'sample_map_refined.inc').write_text('\n'.join(lines)+'\n');(root/'preparation_refined.json').write_text(json.dumps(rows,indent=2)+'\n')
