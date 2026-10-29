"""Verify pinned flute and cello recordings and their prepared loop and dynamics metadata."""
import pathlib, json, hashlib, sys, math, wave, re
root = pathlib.Path(sys.argv[1])
for bank, count, roots, low, high, seam in [('vsco2_flute_v1',18,9,60,93,.88),('vsco2_cello_section_v1',26,13,36,77,.32)]:
    r = root / bank
    manifest = json.loads((r/'provenance.json').read_text())
    assert manifest['license']=='CC0-1.0' and manifest['upstream_commit']=='440300901dfe9275fd84e0b7763af1f8443ae62e'
    assert len(manifest['files'])==count
    frames = {}
    for source in manifest['files']:
        path = r/source['file']; data = path.read_bytes()
        assert len(data)==source['bytes'] and hashlib.sha256(data).hexdigest()==source['sha256'], path
        with wave.open(str(path)) as wav:
            assert (wav.getframerate(),wav.getnchannels(),wav.getsampwidth())==(44100,2,3)
            frames[source['file']]=wav.getnframes()
    rows = json.loads((r/'preparation_refined.json').read_text())
    assert len(rows)==count and len((r/'sample_map_refined.inc').read_text().splitlines())==count
    assert {q['file'] for q in rows}==set(frames)
    notes = {q['note'] for q in rows}
    assert len(notes)==roots and min(notes)==low and max(notes)==high
    for note in notes:
        pair = [q for q in rows if q['note']==note]
        assert len(pair)==2 and {q['layer'] for q in pair}=={0,1}
        assert len({q['loop_start'] for q in pair})==len({q['loop_end'] for q in pair})==1
        assert len({q['blend_correlation'] for q in pair})==1
    for q in rows:
        assert all(math.isfinite(q[k]) for k in ['gain','pitch_ratio','median_cents','blend_correlation','seam_correlation'])
        assert q['gain']>0 and abs(q['median_cents'])<10 and .99<q['pitch_ratio']<1.01
        assert q['loop_period_seconds']>=.8 and seam<=q['seam_correlation']<=1
        assert 0<=q['blend_correlation']<=1 and 0<=q['trim']
        assert 0<=q['loop_start']<q['loop_end'] and q['crossfade']>0
        assert q['trim']+q['loop_end']*q['pitch_ratio']+1<frames[q['file']]
    for filename,key in [('pitch_ratios.inc','pitch_ratio'),('blend_correlation.inc','blend_correlation')]:
        values=[float(v.rstrip(',f')) for v in (r/filename).read_text().splitlines()]
        assert len(values)==count and all(abs(v-q[key])<1e-8 for v,q in zip(values,rows))
    model = json.loads((r/'blend_envelopes.json').read_text())
    assert model['window_frames']==4410 and model['step_maximum_frames']==441
    assert model['minimum_gain']==.75 and model['maximum_gain']==1.35
    curves = model['curves']; assert len(curves)==roots and {q['note'] for q in curves}==notes
    expected_points=[]
    for curve in curves:
        sample=next(q for q in rows if q['note']==curve['note'])
        assert curve['loop_end']==sample['loop_end'] and curve['period']==sample['loop_end']-sample['loop_start']-sample['crossfade']
        assert len(curve['initial'])==math.ceil(curve['loop_end']/441)+1 and len(curve['cycle'])==math.ceil(curve['period']/441)
        assert curve['initial'][-1]==curve['cycle'][0]
        for points in [curve['initial'],curve['cycle']]:
            for soft,strong,covariance in points:
                assert all(math.isfinite(v) for v in [soft,strong,covariance])
                assert soft>=0 and strong>=0 and abs(covariance)<=math.sqrt(soft*strong)+1e-10
            expected_points.extend(points)
    generated=(r/'blend_envelopes.inc').read_text()
    points=[tuple(float(v) for v in q) for q in re.findall(r'\{([-0-9.e+]+)f, ([-0-9.e+]+)f, ([-0-9.e+]+)f\}',generated)]
    assert len(points)==len(expected_points)
    assert all(abs(v-e)<=1e-12+abs(e)*1e-9 for point,expected in zip(points,expected_points) for v,e in zip(point,expected))
    prefix='flute' if 'flute' in bank else 'cello'
    descriptors=re.findall(prefix+r'_blend_(\d+)_initial, (\d+), '+prefix+r'_blend_\d+_cycle, (\d+), (\d+), (\d+)',generated)
    assert len(descriptors)==count
    for q,descriptor in zip(rows,descriptors):
        index,initial,cycle,end,period=map(int,descriptor);curve=curves[index]
        assert q['note']==curve['note'] and (initial,cycle,end,period)==(len(curve['initial']),len(curve['cycle']),curve['loop_end'],curve['period'])
    assert 'CC0' in (r/'LICENSE.txt').read_text()
    print(f'{bank}: {count} pinned CC0 recordings, {roots} roots, two layers and stabilized loops verified')
