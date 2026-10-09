import bpy,bmesh,json,hashlib,math
from pathlib import Path
from mathutils import Vector
R=Path(__file__).resolve().parents[1]
bpy.ops.wm.open_mainfile(filepath=str(R/'KITE01_Modular.blend'))
s=bpy.context.scene;s.cycles.use_denoising=False
checks=[];report={'scope':'Blender native structure and export roundtrip only, not engine or full character collision','blender':bpy.app.version_string,'meshes':[],'glb_roundtrip':[],'fbx_roundtrip':[]}
parts=[o for o in bpy.data.objects if o.type=='MESH' and o.name.startswith('SM_KITE01_')]
for o in parts:
 bm=bmesh.new();bm.from_mesh(o.data);nonman=sum(not e.is_manifold for e in bm.edges);bm.free()
 uv=o.data.uv_layers.active; uv_good=bool(uv) and all(-.00001<=v<=1.00001 for l in uv.data for v in l.uv)
 finite=all(math.isfinite(v) for p in o.data.vertices for v in p.co)
 item={'name':o.name,'nonmanifold_edges':nonman,'uv0_in_0_1':uv_good,'finite':finite,'scale':list(o.scale),'negative_scale':any(v<=0 for v in o.scale)}
 report['meshes'].append(item);checks.extend([nonman==0,uv_good,finite,not item['negative_scale']])
report['missing_textures']=[im.filepath for im in bpy.data.images if im.source=='FILE' and not im.packed_file and not Path(bpy.path.abspath(im.filepath)).exists()];checks.append(not report['missing_textures'])
report['sockets']=[o.name for o in bpy.data.objects if o.name.startswith('SOCKET_')];checks.append(len(report['sockets'])==8)
report['animation_sample_frames']={}
for f in [1,30,45,80,86,96]:
 s.frame_set(f);report['animation_sample_frames'][str(f)]={n:list(bpy.data.objects['SM_KITE01_'+n].location) for n in ['Magazine_Box','ChargingHandle']}
s.frame_set(1);bpy.ops.wm.save_as_mainfile(filepath=str(R/'KITE01_Modular.blend'))
for level in range(3):
 bpy.ops.wm.read_factory_settings(use_empty=True);bpy.ops.import_scene.gltf(filepath=str(R/'exports'/f'KITE01_LOD{level}.glb'))
 meshes=[o for o in bpy.data.objects if o.type=='MESH'];tris=0
 for o in meshes:o.data.calc_loop_triangles();tris+=len(o.data.loop_triangles)
 bounds=[o.matrix_world@Vector(v) for o in meshes for v in o.bound_box]
 dims=[max(v[i] for v in bounds)-min(v[i] for v in bounds) for i in range(3)]
 item={'lod':level,'mesh_count':len(meshes),'triangles':tris,'bounds_m':dims,'actions':[a.name for a in bpy.data.actions]};report['glb_roundtrip'].append(item);checks.append(len(meshes)==9)
for p in sorted((R/'exports').glob('*.fbx')):
 bpy.ops.wm.read_factory_settings(use_empty=True);bpy.ops.import_scene.fbx(filepath=str(p));meshes=[o for o in bpy.data.objects if o.type=='MESH']
 item={'filename':p.name,'mesh_count':len(meshes),'object_origins':[list(o.location) for o in meshes],'uv_present':all(bool(o.data.uv_layers) for o in meshes),'materials':len(bpy.data.materials)};report['fbx_roundtrip'].append(item);checks.extend([len(meshes)==1,item['uv_present'],all(o.location.length<1e-5 for o in meshes)])
report['all_scoped_checks_pass']=all(checks);report['checks_count']=len(checks)
report['limitations']=['No UE execution or acceptance','No collision-free hands/body/reload claim','Object animation study only, not skeletal character reload','LOD visual quality not equivalent to production optimization','Exterior game prop; no functional internals']
(R/'docs'/'Blender_Validation.json').write_text(json.dumps(report,indent=2));print('VALIDATION',report['all_scoped_checks_pass'])
