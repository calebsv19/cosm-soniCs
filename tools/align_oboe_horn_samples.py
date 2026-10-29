"""Align two recorded sustain clocks while retaining source attacks and the soft take's pitch movement."""
import pathlib,json,hashlib,wave,struct,argparse,numpy as np
from sample_bank_pcm import decode_centered_pcm
parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('bank_root',type=pathlib.Path);parser.add_argument('--banks',nargs='+',choices=['vsco2_oboe_v1','vsco2_french_horn_v1','vsco2_bassoon_v1','vsco2_tenor_trombone_v1','vsco2_violin_section_v1'],default=['vsco2_oboe_v1','vsco2_french_horn_v1']);args=parser.parse_args();base=args.bank_root;rate=44100

def phase_clock(x,hz,harmonic=1):
    """Track a smoothed fundamental phase independently of the recording's harmonic timbre."""
    n=1<<(len(x)-1).bit_length();freq=np.fft.rfftfreq(n,1/rate);distance=abs(freq-hz*harmonic)/hz;mask=np.zeros(len(freq));mask[distance<=.30]=1;edge=(distance>.30)&(distance<.45);mask[edge]=.5+.5*np.cos(np.pi*(distance[edge]-.30)/.15)
    spectrum=np.zeros(n,dtype=complex);spectrum[:len(freq)]=np.fft.rfft(x,n)*mask;analytic=np.fft.ifft(spectrum)[:len(x)]*2
    omega=2*np.pi*hz*harmonic/rate;nominal=np.arange(len(x))*omega;residual=np.unwrap(np.angle(analytic))-nominal;width=1323;pad=np.pad(residual,(width//2,width//2),mode='edge');prefix=np.r_[0.,np.cumsum(pad)];smooth=(prefix[width:]-prefix[:-width])/width;phase=nominal+smooth;phase=np.r_[phase[0],phase[0]+np.cumsum(np.clip(np.diff(phase),omega*.8,omega*1.2))]
    lo=rate;hi=min(len(x)-rate,rate*5);confidence=float(np.sqrt(np.mean(abs(analytic[lo:hi])**2))/max(1e-10,np.sqrt(np.mean(x[lo:hi]**2))))
    return phase/harmonic,confidence

def write_pcm(path,x):
    """Retain a lossless-precision mono prepared waveform as ordinary 24-bit PCM."""
    integers=np.rint(np.clip(x,-1,1-1/8388608)*8388608).astype(np.int32);b=np.stack([integers&255,(integers>>8)&255,(integers>>16)&255],axis=1).astype(np.uint8)
    with wave.open(str(path),'wb') as w:w.setnchannels(1);w.setsampwidth(3);w.setframerate(rate);w.writeframes(b.tobytes())
    if path.stat().st_size%2:
        data=bytearray(path.read_bytes());data+=b'\0';struct.pack_into('<I',data,4,len(data)-8);path.write_bytes(data)

for bank in args.banks:
    root=base/bank;rows=json.load(open(root/'preparation.json'));prepared=root/'prepared';prepared.mkdir(exist_ok=True);records=[]
    for note in sorted({q['note'] for q in rows}):
        pair=sorted([q for q in rows if q['note']==note],key=lambda q:q['layer']);signals=[]
        for q in pair:
            source_rate,x=decode_centered_pcm(root/q['file']);assert source_rate==rate;x=x[q['trim']:];ratio=q['pitch_ratio'];signals.append(np.interp(np.arange(int((len(x)-1)/ratio))*ratio,np.arange(len(x)),x))
        best=None
        for harmonic in range(1,9):
            clocks=[];conf=[]
            for x in signals:
                clock,confidence=phase_clock(x,pair[0]['expected_hz'],harmonic);clocks.append(clock);conf.append(confidence)
            score=min(conf)
            if best is None or score>best[0]:best=(score,harmonic,clocks,conf)
            if harmonic==1 and score>.08:break
        score,harmonic,clocks,conf=best
        assert score>.05,('No reliable shared harmonic',bank,note,score)
        time=np.arange(len(signals[1]));reference=np.interp(time,np.arange(len(clocks[0])),clocks[0]);outside=time>=len(clocks[0]);reference[outside]=clocks[0][-1]+(time[outside]-(len(clocks[0])-1))*2*np.pi*pair[0]['expected_hz']/rate
        offset=clocks[1][13230]-clocks[0][13230];desired=np.interp(reference+offset,clocks[1],np.arange(len(clocks[1])));fade=np.clip((time-13230)/22050,0,1);fade=fade*fade*(3-2*fade);positions=time+(desired-time)*fade
        signals[1]=np.interp(positions,time,signals[1]);active=(time>=35280)&(time<min(len(signals[1])-rate,rate*6));shift=float(np.max(abs(positions[active]-time[active]))/rate);clock_rates=np.diff(positions)[35280:min(len(positions)-rate,rate*6)];assert min(clock_rates)>.8 and max(clock_rates)<1.2,(bank,note,min(clock_rates),max(clock_rates))
        for q,x in zip(pair,signals):
            path=prepared/q['file'];write_pcm(path,x);records.append(dict(file=q['file'],note=note,layer=q['layer'],sha256=hashlib.sha256(path.read_bytes()).hexdigest(),bytes=path.stat().st_size,source_sha256=hashlib.sha256((root/q['file']).read_bytes()).hexdigest(),trim=q['trim'],original_pitch_ratio=q['pitch_ratio'],phase_guide_harmonic=harmonic,fundamental_confidence=conf[q['layer']],sustain_clock_alignment=q['layer']==1,max_active_clock_shift_seconds=shift if q['layer'] else 0))
        print(bank,note,'shift ms',round(shift*1000,3),'confidence',conf,flush=True)
    (prepared/'provenance.json').write_text(json.dumps(dict(algorithm='paired-fundamental-phase-clock-v1',attack_identity_until_reference_frame=13230,full_alignment_reference_frame=35280,files=records),indent=2)+'\n')
