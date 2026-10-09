"""Package the standalone weapon only. Character fit remains a separate deliverable."""
import pathlib,json,zipfile,hashlib
R=pathlib.Path(__file__).resolve().parents[1]
files=[]
for p in sorted(R.rglob('*')):
 if not p.is_file() or p.suffix in ['.blend1','.log','.pyc'] or '__pycache__' in p.parts or any(x in p.name.lower() for x in ['character','file_index','delivery_manifest']):continue
 files.append({'path':str(p.relative_to(R)),'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()})
(R/'docs/File_Index.json').write_text(json.dumps({'stage':'weapon_only_v1','files':files},indent=2))
out=R.parent/'KITE01_WeaponAsset_v1.zip'
with zipfile.ZipFile(out,'w',zipfile.ZIP_DEFLATED) as z:
 for item in files:z.write(R/item['path'],'KITE01/'+item['path'])
 z.write(R/'docs/File_Index.json','KITE01/docs/File_Index.json')
with zipfile.ZipFile(out) as z:
 assert z.testzip() is None
 for it in files:assert hashlib.sha256(z.read('KITE01/'+it['path'])).hexdigest()==it['sha256']
print('ZIP verified',len(files),'files',hashlib.sha256(out.read_bytes()).hexdigest())
