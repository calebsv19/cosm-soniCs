"""Verify the pinned CC0 violin bank independently before packaging."""
import hashlib,json,pathlib,sys
root=pathlib.Path(sys.argv[1])
manifest=json.loads((root/'provenance.json').read_text())
assert manifest['license']=='CC0-1.0' and len(manifest['files'])==30
for item in manifest['files']:
    data=(root/item['file']).read_bytes()
    assert len(data)==item['bytes'] and hashlib.sha256(data).hexdigest()==item['sha256'],item['file']
assert (root/'LICENSE.txt').stat().st_size>1000
assert len((root/'sample_map.inc').read_text().splitlines())==30
assert len((root/'sample_map_stabilized.inc').read_text().splitlines())==30
prepared=json.loads((root/'preparation_stabilized.json').read_text())
assert len(prepared)==30 and {q['file'] for q in prepared}=={q['file'] for q in manifest['files']}
for q in prepared:
    assert q['loop_period_seconds']>=1.8 and 0<=q['seam_correlation']<=1 and q['gain']>0
print('Pinned violin bank: all 30 hashes and CC0 license verified')
