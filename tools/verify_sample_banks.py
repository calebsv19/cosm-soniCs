"""Verify fixed trumpet/clarinet bank bytes and prepared layer/pitch coverage."""
import pathlib,json,hashlib,sys,math
base=pathlib.Path(sys.argv[1])
for name,count,layers in [('vsco2_trumpet_v1',20,2),('vsco2_clarinet_v1',33,3)]:
 root=base/name;manifest=json.loads((root/'provenance.json').read_text());assert manifest['license']=='CC0-1.0' and len(manifest['files'])==count
 for item in manifest['files']:
  data=(root/item['file']).read_bytes();assert len(data)==item['bytes'] and hashlib.sha256(data).hexdigest()==item['sha256'],item['file']
 prepared=json.loads((root/'preparation_stabilized.json').read_text());assert len(prepared)==count and len((root/'sample_map_stabilized.inc').read_text().splitlines())==count
 for note in {p['note'] for p in prepared}:
  assert {p['layer'] for p in prepared if p['note']==note}==set(range(layers))
 for p in prepared:
  assert math.isfinite(p['sustain_log_rms_std']) and p['sustain_log_rms_std']>=0
  assert p['loop_period_seconds']>=1.8 and 0<=p['seam_correlation']<=1 and math.isfinite(p['gain']) and p['gain']>0
 assert (root/'LICENSE.txt').stat().st_size>1000
 print(name+': pinned assets and complete pitch/layer mapping verified')
