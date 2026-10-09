import bpy,json,hashlib
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
p=ROOT/'docs/Character_Fit_Study.json';r=json.loads(p.read_text())
bpy.ops.wm.open_mainfile(filepath=str(ROOT/'KITE01_CharacterFit.blend'))
images=[{'name':i.name,'packed':bool(i.packed_file),'width':i.size[0],'height':i.size[1]} for i in bpy.data.images if i.source=='FILE']
r['saved_blend_readback']={'image_count':len(images),'all_file_images_packed':all(i['packed'] for i in images),'images':images,'source_inputs_current_sha256':{k:hashlib.sha256((ROOT.parent/k).read_bytes()).hexdigest() for k in r['inputs_sha256_before']}}
r['saved_blend_readback']['source_inputs_still_exact']=r['saved_blend_readback']['source_inputs_current_sha256']==r['inputs_sha256_before']
p.write_text(json.dumps(r,ensure_ascii=False,indent=2));print('FINAL_READBACK',json.dumps(r['saved_blend_readback']))
