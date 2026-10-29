"""Verify the pinned CC0 sax bank, native rate and complete two-layer pitch mapping."""
import pathlib,json,hashlib,sys,math,wave
r=pathlib.Path(sys.argv[1]);m=json.loads((r/'provenance.json').read_text());assert m['license']=='CC0-1.0' and m['commit']=='c1ea7bcc3c7309650ab0da9d15c9cd1fbc4a4c7e' and len(m['files'])==46
for q in m['files']:
 data=(r/q['file']).read_bytes();assert len(data)==q['bytes'] and hashlib.sha256(data).hexdigest()==q['sha256'],q['file']
 with wave.open(str(r/q['file'])) as w:assert w.getframerate()==48000 and w.getnchannels()==2 and w.getsampwidth()==3
rows=json.loads((r/'preparation_stabilized.json').read_text());assert len(rows)==46 and len((r/'sample_map_stabilized.inc').read_text().splitlines())==46
notes={q['note'] for q in rows};assert len(notes)==23 and min(notes)==44 and max(notes)==88
for note in notes:
 pair=[q for q in rows if q['note']==note];assert {q['layer'] for q in pair}=={0,1}
 assert len({q['loop_start'] for q in pair})==len({q['loop_end'] for q in pair})==1
 assert all(0<=q['blend_correlation']<=1 and 0<=q['phase_alignment_frames']<=round(48000/q['expected_hz']) for q in pair)
values=[float(v.rstrip(',f')) for v in (r/'blend_correlation.inc').read_text().splitlines()]
assert len(values)==46 and all(math.isfinite(v) and 0<=v<=1 and abs(v-q['blend_correlation'])<1e-8 for v,q in zip(values,rows))
for q in rows:
 assert q['source_rate']==48000 and q['loop_period_seconds']>=.8 and (.65 if q['note'] in (78,88) else .85)<=q['seam_correlation']<=1
 assert math.isfinite(q['gain']) and q['gain']>0 and abs(q['cents'])<5 and math.isfinite(q['sustain_log_rms_std'])
assert 'CC0' in (r/'LICENSE.txt').read_text()
print('Saxophone: 46 pinned CC0 samples, 23 sounding roots, two layers and native-rate loops verified')
