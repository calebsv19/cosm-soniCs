"""Headless qualification against published EBU signal definitions and an independent FFmpeg meter."""
import argparse
from array import array
import hashlib
import json
import math
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

# Generates deterministic original-level interleaved sine samples without an optional numeric dependency.
def tone(rate, seconds, db=-23, channels=2, hz=1000, phase=0, taper=False):
    count=round(rate*seconds); amplitude=10**(db/20); data=array('f')
    # Integer-period patterns keep even the long gate fixtures cheap to regenerate.
    divisor=math.gcd(rate,round(hz)) if hz==round(hz) else 1
    period=rate//divisor
    if not taper:
        pattern=array('f',(amplitude*math.sin(2*math.pi*hz*f/rate+phase) for f in range(period) for _ in range(channels)))
        data=pattern*(count//period)+pattern[:(count%period)*channels]
    else:
        fade=rate*.01
        data=array('f',(amplitude*math.sin(2*math.pi*hz*f/rate+phase)*min(1,f/fade,(count-1-f)/fade) for f in range(count) for _ in range(channels)))
    return data

# Parses the independent filter's final integrated/true-peak summaries and its last complete windows.
def reference(ffmpeg,path,rate,channels):
    p=subprocess.run([ffmpeg,'-hide_banner','-nostats','-f','f32le','-ar',str(rate),'-ac',str(channels),'-i',str(path),'-af','ebur128=peak=true','-f','null','-'],capture_output=True,text=True,check=True)
    summary=p.stderr.rsplit('Summary:',1)[1]
    integrated=float(re.search(r'I:\s*([-+0-9.]+) LUFS',summary)[1])
    peak=float(re.search(r'Peak:\s*([-+0-9.]+) dBFS',summary)[1])
    windows=re.findall(r'M:\s*([-+0-9.]+) S:\s*([-+0-9.]+)',p.stderr)
    return dict(LRA=float(re.search(r'LRA:\s*([-+0-9.]+) LU',summary)[1]),I=integrated,TP=peak,M=float(windows[-1][0]),S=float(windows[-1][1]))

# Verifies standards-defined levels, gating, intersample peaks, block invariance, boundary flush and unsupported layouts.
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--binary',required=True);ap.add_argument('--output-root',required=True);args=ap.parse_args()
    ffmpeg=shutil.which('ffmpeg');assert ffmpeg,'FFmpeg is required for independent loudness qualification'
    output=Path(args.output_root);output.mkdir(parents=True,exist_ok=True);rows=[]
    with tempfile.TemporaryDirectory(prefix='sonics-loudness-signals-') as directory:
        root=Path(directory)
        def measure(name,data,rate=48000,channels=2,block=257,compare=False,programme=False):
            path=root/(name+'.f32')
            with path.open('wb') as file:data.tofile(file)
            result=json.loads(subprocess.check_output([args.binary,str(rate),str(channels),str(block),str(path)]+(["programme"] if programme else []),text=True))
            row=dict(name=name,rate=rate,channels=channels,block=block,input_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),result=result)
            if compare:
                ref=reference(ffmpeg,path,rate,channels);row['reference']=ref
                for key in ('I','M','S'):
                    if result[key] is not None:assert abs(result[key]-ref[key])<.06,(name,key,result[key],ref[key])
                if result['tp'][0] is not None:assert abs(max(result['tp'])-ref['TP'])<.4,(name,result['tp'],ref)
            if programme and compare and result['LRA'] is not None:assert abs(result['LRA']-ref['LRA'])<1,(name,result['LRA'],ref['LRA'])
            rows.append(row);return result
        for rate in (44100,48000,96000):
            for db in (-23,-33):
                result=measure(f'EBU-definition-{db}-{rate}',tone(rate,20,db),rate,compare=True)
                for key in ('I','M','S'):assert abs(result[key]-db)<.1,(rate,key,result[key])
            mono=measure(f'mono-{rate}',tone(rate,4,channels=1),rate,1,compare=True)
            stereo=measure(f'stereo-{rate}',tone(rate,4),rate,compare=True)
            assert abs(stereo['I']-mono['I']-10*math.log10(2))<1e-8
            low=tone(rate,4,-14,hz=100);high=tone(rate,4,-20,hz=7000)
            measure(f'K-weight-spectrum-{rate}',array('f',(a+b for a,b in zip(low,high))),rate,compare=True)
            for case,divisor,phase,amplitude,expected in ((15,4,0,.5,-6),(16,4,45,.5,-6),(17,6,60,.5,-6),(18,8,67.5,.5,-6),(19,4,45,1.41,3)):
                result=measure(f'EBU-TP-{case}-{rate}',tone(rate,.2,20*math.log10(amplitude),hz=rate/divisor,phase=math.radians(phase),taper=True),rate,compare=True)
                assert all(expected-.4<=v<=expected+.2 for v in result['tp']),(rate,case,result['tp'])
        # Long definitions qualify the same pinned gate implementation directly after the wrapper's 30 s history cap.
        for case,segments in ((3,[(10,-36),(60,-23),(10,-36)]),(4,[(10,-72),(10,-36),(60,-23),(10,-36),(10,-72)]),(5,[(20,-26),(20.1,-20),(20,-26)])):
            data=sum((tone(48000,s,db) for s,db in segments),array('f'))
            result=measure(f'EBU-gate-{case}',data,compare=True);assert abs(result['I']+23)<.1,(case,result['I'])
        data=sum((tone(48000,s,db) for _ in range(5) for s,db in ((1.34,-20),(1.66,-30))),array('f'))
        result=measure('EBU-shortterm-9',data,compare=True)
        assert all(abs(p[2]+23)<.1 for p in result['history'] if p[0]>=3*48000)
        data=sum((tone(48000,s,db) for _ in range(25) for s,db in ((.18,-20),(.22,-30))),array('f'))
        result=measure('EBU-momentary-12',data,compare=True)
        assert all(abs(p[1]+23)<.1 for p in result['history'] if p[0]>=48000)
        for rate in (44100,48000,96000):
            for case,levels,expected in ((1,(-20,-30),10),(2,(-20,-15),5),(3,(-40,-20),20),(4,(-50,-35,-20,-35,-50),15)):
                data=sum((tone(rate,20,db) for db in levels),array('f'))
                result=measure(f'EBU3342-LRA-{case}-{rate}',data,rate,compare=True,programme=True)
                assert abs(result['LRA']-expected)<=1,(rate,case,result['LRA'])
                assert result['lra_windows']==round((len(data)/2/rate-3)*10)+1
        empty=measure('programme-silence',array('f',[0])*48000*2*5,programme=True);assert empty['LRA'] is None and empty['I'] is None
        short_lra=measure('programme-too-short',tone(48000,2),programme=True);assert short_lra['LRA'] is None and short_lra['lra_windows']==0
        programme_a=measure('programme-partition-a',tone(48000,4.057),block=1,programme=True)
        programme_b=measure('programme-partition-b',tone(48000,4.057),block=4096,programme=True)
        assert programme_a==programme_b and programme_a['lra_windows']==11
        # Fractional endpoint, partitioning and maximum storage are real capture wrapper cases.
        data=tone(48000,3.051,db=-18)
        a=measure('partition-a',data,block=1);b=measure('partition-b',data,block=4096)
        assert a==b and a['history'][-1][0]==round(48000*3.051)
        maximum=measure('30-second-bound',tone(48000,30));assert len(maximum['history'])==300
        silence=measure('silence',array('f',[0])*(48000*2));assert silence['I'] is None and silence['M'] is None and all(v is None for v in silence['tp'])
        short=measure('short',tone(48000,.1));assert short['I'] is None and short['M'] is None and short['S'] is None
        impulse=array('f',[0])*(4800*2);impulse[-2]=.8;impulse[-1]=.4
        result=measure('terminal-impulse',impulse);assert result['tp'][0]>=20*math.log10(.8)-.001 and abs(result['tp'][0]-result['tp'][1]-20*math.log10(2))<1e-7
        edge=array('f',[0])*(4800*2)
        for f in range(4):edge[-8+2*f]=.9*(-1)**f;edge[-7+2*f]=.45*(-1)**f
        result=measure('terminal-intersample',edge);assert result['tp'][0]>result['unflushed_tp'][0]+.1,(result['tp'],result['unflushed_tp'])
        for rate,channels in ((48000,6),(22050,2),(192000,2)):
            result=measure(f'unsupported-{rate}-{channels}',array('f',[0])*channels*10,rate,channels);assert not result['available']
    # Large traces stay rerunnable; retain compact maxima/endpoints and exact source hashes instead of PCM.
    for row in rows:
        history=row['result'].pop('history');row['history_points']=len(history)
        row['first_history']=history[0] if history else None;row['last_history']=history[-1] if history else None
    (output/'qualification.json').write_text(json.dumps(dict(passed=True,cases=len(rows),boundary='reconstructed EBU Tech 3341 definitions 1-5,9,12,15-19 and Tech 3342 definitions 1-4 at 10 Hz; official ZIP unavailable HTTP 403; not full EBU certification; independent FFmpeg and analytic edge cases',ffmpeg_version=subprocess.check_output([ffmpeg,'-version'],text=True).splitlines()[0],results=rows),indent=2)+'\n')
    print(f'loudness qualification passed: {len(rows)} cases; analytic, gate, partition, window, end-flush and independent FFmpeg proof')
if __name__=='__main__':main()
