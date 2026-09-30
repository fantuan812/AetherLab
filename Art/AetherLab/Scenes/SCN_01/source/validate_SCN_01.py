from pathlib import Path
import bpy,os,json
from mathutils import Vector
R = str(Path(__file__).resolve().parents[1])
results=[]
def bounds(obs):
 pts=[o.matrix_world@Vector(c) for o in obs if o.type=='MESH' for c in o.bound_box]
 return [round(max(p[i] for p in pts)-min(p[i] for p in pts),4) for i in range(3)] if pts else []
for f in sorted(os.listdir(R+'/exports/fbx')):
 if not f.endswith('.fbx'):continue
 bpy.ops.wm.read_factory_settings(use_empty=True);bpy.ops.import_scene.fbx(filepath=R+'/exports/fbx/'+f);bpy.context.view_layer.update()
 obs=list(bpy.data.objects);meshes=[o for o in obs if o.type=='MESH'];results.append({'file':f,'status':'reimport_pass','objects':len(obs),'meshes':len(meshes),'dimensions_m':bounds(meshes),'separate_water_meshes':sum(o.name.startswith('Contained water') for o in meshes)})
bpy.ops.wm.read_factory_settings(use_empty=True);bpy.ops.import_scene.gltf(filepath=R+'/exports/SCN_01_Assembled.glb');bpy.context.view_layer.update();obs=list(bpy.data.objects)
results.append({'file':'SCN_01_Assembled.glb','status':'reimport_pass','objects':len(obs),'meshes':sum(o.type=='MESH' for o in obs),'dimensions_m':bounds(obs),'separate_water_meshes':sum(o.name.startswith('Contained water') for o in obs)})
with open(R+'/docs/export_reimport_check.json','w') as f:json.dump({'application':'Blender 4.3.2','acceptance_scope':'Blender file/readability and export reimport; not UE integration','files':results},f,indent=2)
print(json.dumps(results,indent=2))
