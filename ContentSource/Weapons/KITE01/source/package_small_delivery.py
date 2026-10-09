"""Standalone small archives, not split-volume chunks. Each ZIP can be opened alone."""
from pathlib import Path
import zipfile,json,hashlib
R=Path(__file__).resolve().parents[1];O=R.parent/'delivery_parts';O.mkdir(exist_ok=True)
groups={
 'KITE01_GLB_LOD0.zip':[R/'exports/KITE01_LOD0.glb'],
 'KITE01_GLB_LOD1_LOD2.zip':[R/'exports/KITE01_LOD1.glb',R/'exports/KITE01_LOD2.glb'],
 'KITE01_FBX_MainFurniture.zip':[R/'exports'/('SM_KITE01_'+n+'.fbx') for n in ['Receiver','Handguard_Sand','Stock_Skeleton']],
 'KITE01_FBX_GripMagazineOptic.zip':[R/'exports'/('SM_KITE01_'+n+'.fbx') for n in ['Grip_Angled','Magazine_Box','Optic_Reflex']],
 'KITE01_FBX_VariantsMoving.zip':[R/'exports'/('SM_KITE01_'+n+'.fbx') for n in ['Muzzle_Short','Muzzle_Cover','Stock_Compact','ChargingHandle','Trigger']],
 'KITE01_TexturesSourceDocs.zip':[p for folder in ['textures','source','docs','reference'] for p in (R/folder).rglob('*') if p.is_file() and p.suffix in ['.png','.py','.md','.json'] and '__pycache__' not in p.parts and p.name not in ['KITE01_Concept.png','File_Index.json','Delivery_Manifest.json']]
}
rows=[]
for name,paths in groups.items():
 out=O/name
 with zipfile.ZipFile(out,'w',zipfile.ZIP_DEFLATED) as z:
  for p in paths:z.write(p,'KITE01/'+str(p.relative_to(R)))
  z.write(R/'README.zh-CN.md','KITE01/README.zh-CN.md')
  if 'SourceDocs' not in name:z.write(R/'docs/Build_Manifest.json','KITE01/docs/Build_Manifest.json')
  z.writestr('ABOUT_THIS_PACKAGE.txt','Standalone subset of KITE01 game art. Open this ZIP independently; no multi-volume reconstruction. GLB contains material images. FBX individual modules use local pivots; see Build_Manifest for visual assembly locations. Full PBR textures and reproducible scripts are in TexturesSourceDocs. No engine acceptance or real-world manufacturing data.\n')
 with zipfile.ZipFile(out) as z:assert z.testzip() is None
 assert out.stat().st_size < 8*1024*1024,(name,out.stat().st_size)
 rows.append({'filename':name,'bytes':out.stat().st_size,'sha256':hashlib.sha256(out.read_bytes()).hexdigest(),'members':[str(p.relative_to(R)) for p in paths]})
(O/'Small_Delivery_Index.json').write_text(json.dumps(rows,indent=2));print(json.dumps(rows,indent=2))
print('NATIVE_BYTES',(R/'KITE01_Modular.blend').stat().st_size,'FULL_ZIP_BYTES',(R.parent/'KITE01_WeaponAsset_v1.zip').stat().st_size)
