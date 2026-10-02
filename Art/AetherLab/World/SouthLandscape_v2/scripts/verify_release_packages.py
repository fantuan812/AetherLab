"""Read-only ZIP evidence with extraction to a new review directory."""
from pathlib import Path
import argparse,json,hashlib,zipfile
p=argparse.ArgumentParser();p.add_argument('--model',required=True);p.add_argument('--textures',required=True);p.add_argument('--views',required=True);p.add_argument('--output',required=True);p.add_argument('--report',required=True);a=p.parse_args()
out=Path(a.output);out.mkdir(parents=True,exist_ok=True);sha=lambda x:hashlib.sha256(x).hexdigest();seen={};rows=[];manifest_count=0
for package in [a.model,a.textures,a.views]:
 path=Path(package);assert path.stat().st_size<14*1024*1024
 with zipfile.ZipFile(path) as z:
  assert z.testzip() is None;names=z.namelist();assert len(names)==len(set(names))
  for n in names:
   assert not n.startswith('/') and '..' not in Path(n).parts
   b=z.read(n);digest=sha(b)
   if n in seen:assert seen[n]==digest,('conflicting cross-package member',n)
   seen[n]=digest
  z.extractall(out)
  manifests=[n for n in names if Path(n).name in {'Package_Content_Model.json','Package_Content_Views.json'}]
  for m in manifests:
   content=json.loads(z.read(m));assert {r['path'] for r in content}==set(names)-{m}
   for r in content:
    f=out/r['path'];assert f.stat().st_size==r['bytes'] and sha(f.read_bytes())==r['sha256']
   manifest_count+=1
  rows.append({'file':path.name,'bytes':path.stat().st_size,'sha256':sha(path.read_bytes()),'members':len(names),'crc_passed':True,'safe_paths':True,'manifest_files':manifests})
assert manifest_count==2
report={'packages':rows,'all_under_14MiB':True,'cross_package_conflicting_paths':[],'model_and_views_content_manifests_verified':True,'passed':True}
Path(a.report).write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2))
