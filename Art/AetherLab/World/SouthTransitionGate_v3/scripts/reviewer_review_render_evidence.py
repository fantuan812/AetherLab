"""Compare render receipts against actual image bytes and expected frozen sources."""
from pathlib import Path
import json,hashlib,argparse
p=argparse.ArgumentParser();p.add_argument('--root',default=str(Path(__file__).resolve().parents[1]));p.add_argument('--expected-sha',required=True);a=p.parse_args();r=Path(a.root);base='2125fbd4f5887aea272240581c062b5adbb0ce66c557b71f236dcd82b0c00ca7';sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest();allviews={}
for sub,expected in [('before',base),('release_after',a.expected_sha)]:
 for f in sorted((r/sub).glob('Render_Receipt_*.json')):
  d=json.load(open(f))
  if d['source_sha256_at_start']!=expected:continue
  for v in d['views']:
   if sha(r/sub/v['file'])==v['sha256']:allviews[(sub,v['file'])]=(f,d,v)
checks=[]
for name in ['Ridge_Reveal','Gate','Entry','Town_Reveal','South_Overview','Global']:
 b=allviews[('before',name+'.png')];c=allviews[('release_after',name+'.png')]
 row={'name':name,'before_source':b[1]['source_sha256_at_start'],'after_source':c[1]['source_sha256_at_start'],'before_unchanged':b[1]['source_unchanged'],'after_unchanged':c[1]['source_unchanged'],'settings_equal':all(b[2][k]==c[2][k] for k in ['camera','matrix_world','lens_mm','ortho_scale','resolution','samples']),'images_hash_match_receipts':True,'resolution':c[2]['resolution'],'samples':c[2]['samples'],'before_image_sha256':b[2]['sha256'],'after_image_sha256':c[2]['sha256']};checks.append(row)
report={'script_sha256':sha(__file__),'method':'Compared six receipt camera matrices, lens/ortho settings, resolutions, samples and actual image SHA256. Independent visual findings are in the final review report.','views':checks,'all_checks_match':all(c['settings_equal'] and c['images_hash_match_receipts'] and c['before_unchanged'] and c['after_unchanged'] for c in checks),'no_final_world_or_UE_acceptance':True};(r/'independent_review/Render_Evidence_Review.json').write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2))
