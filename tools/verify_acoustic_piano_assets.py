"""Verify attributed original piano FLACs, finite prepared strikes and complete velocity zones."""
import pathlib,json,hashlib,sys,wave,math,re
r=pathlib.Path(sys.argv[1])/'salamander_piano_v1';original=json.loads((r/'provenance.json').read_text());assert original['license']=='CC-BY-3.0' and original['author']=='Alexander Holm' and len(original['files'])==90
for q in original['files']:
 p=r/q['file'];assert p.stat().st_size==q['bytes'] and hashlib.sha256(p.read_bytes()).hexdigest()==q['sha256']
prepared=json.loads((r/'prepared/provenance.json').read_text());rows=json.loads((r/'preparation.json').read_text());assert len(prepared['files'])==len(rows)==90
sources={q['file']:q for q in original['files']};notes={q['note'] for q in rows};assert len(notes)==30 and min(notes)==21 and max(notes)==108
for n in notes:assert {q['layer'] for q in rows if q['note']==n}=={0,1,2}
for q in prepared['files']:
 p=r/q['file'];assert p.stat().st_size==q['bytes'] and hashlib.sha256(p.read_bytes()).hexdigest()==q['sha256'];assert q['source_sha256']==sources[q['source']]['sha256']
 with wave.open(str(p)) as w:
  assert (w.getframerate(),w.getnchannels(),w.getsampwidth())==(48000,1,3);assert w.getnframes()>4800;w.setpos(w.getnframes()-1);assert w.readframes(1)==b'\0\0\0'
assert all(q['loop_start']==q['loop_end']==q['crossfade']==0 and math.isfinite(q['gain']) and q['gain']>0 for q in rows)
assert len({q['gain'] for q in rows})==1 and len((r/'sample_map.inc').read_text().splitlines())==90
assert 'Creative Commons' in (r/'LICENSE.txt').read_text()
print('Salamander piano: 90 attributed originals/strikes, 30 roots, three velocity zones and finite endpoints verified')

expression=r/'expression';manifest=json.loads((expression/'provenance.json').read_text());assert manifest['license']=='CC-BY-3.0' and manifest['upstream_commit']==original['upstream_commit'] and len(manifest['files'])==92
assert len((expression/'sample_map.inc').read_text().splitlines())==92
for item in manifest['files']:
 source=expression/item['file'];target=expression/item['prepared']
 assert source.stat().st_size==item['bytes'] and hashlib.sha256(source.read_bytes()).hexdigest()==item['sha256']
 assert hashlib.sha256(target.read_bytes()).hexdigest()==item['prepared_sha256']
 with wave.open(str(target)) as w:
  assert (w.getframerate(),w.getnchannels(),w.getsampwidth())==(48000,1,3) and w.getnframes()==item['frames'] and 2<=w.getnframes()<=48000
  assert w.readframes(1)==b'\0\0\0';w.setpos(w.getnframes()-1);assert w.readframes(1)==b'\0\0\0'
print('Piano expression: 88 chromatic key-release and four pedal-action recordings, source/derived hashes and quiet endpoints verified')
