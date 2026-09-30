from pathlib import Path
import bpy,json,os
from mathutils import Vector
R = str(Path(__file__).resolve().parents[1])
bpy.ops.wm.open_mainfile(filepath=R+'/source/SCN_01_Rain_Mountain_Path.blend')
s=bpy.context.scene
# Remove optional off-stage backdrop rocks that clipped awkwardly in the overview.
for o in list(bpy.data.objects):
 if o.name.startswith('Distant karst peak'):bpy.data.objects.remove(o,do_unlink=True)
# Plant the exposed front-left tree into the shelf rather than on its rounded edge.
for o in bpy.data.objects:
 if o.name=='Tree trunk.004' or (o.name.startswith('Tree bough.') and int(o.name.rsplit('.',1)[1])>=28) or (o.name.startswith('Tree canopy.') and int(o.name.rsplit('.',1)[1])>=28):o.location.x+=.7
# Portable reaction-object exports, keeping every mesh distinct.
for name,objs in [('SM_ShelteredBrazierFire',list(bpy.data.collections['FX_ShelteredFire'].objects)),('SM_BarrelWater',[o for o in bpy.data.objects if o.name.startswith('Contained water')])]:
 bpy.ops.object.select_all(action='DESELECT')
 for o in objs:o.select_set(True)
 bpy.context.view_layer.objects.active=objs[0]
 bpy.ops.export_scene.fbx(filepath=R+'/exports/fbx/'+name+'.fbx',use_selection=True,apply_unit_scale=True,axis_forward='-Z',axis_up='Y',object_types={'MESH','EMPTY'},use_mesh_modifiers=True,add_leaf_bones=False,bake_anim=False)
bpy.ops.object.select_all(action='DESELECT')
for o in bpy.data.objects:
 if o.type in ['MESH','EMPTY'] and not o.name.startswith('UCX_'):o.select_set(True)
bpy.ops.export_scene.gltf(filepath=R+'/exports/SCN_01_Assembled.glb',export_format='GLB',use_selection=True,export_draco_mesh_compression_enable=False,export_materials='EXPORT')
s.cycles.samples=64;s.cycles.use_denoising=False
s.render.filepath=R+'/previews/SCN_01_Overview.png'
bpy.ops.wm.save_as_mainfile(filepath=R+'/source/SCN_01_Rain_Mountain_Path.blend')
mesh=[o for o in bpy.data.objects if o.type=='MESH']
with open(R+'/docs/geometry_check.json','w') as f:json.dump({'objects':len(bpy.data.objects),'mesh_objects':len(mesh),'base_mesh_vertices':sum(len(o.data.vertices) for o in mesh),'base_mesh_polygons':sum(len(o.data.polygons) for o in mesh),'note':'Counts exclude modifier-evaluated tessellation. Scene opens in Blender; UE not tested.','units':'Meters; importer must convert to centimeters in UE.','separate_mesh_checks':{key:sum(o.name.startswith(key) for o in mesh) for key in ['Contained water','Wheel','Broken rim','Brazier','Stylized flame','Curved oak stave','Portal']}},f,indent=2)
bpy.ops.render.render(write_still=True)
cam=s.camera;cam.location=(8,-13,9);cam.rotation_euler=(Vector((-3,.1,2))-cam.location).to_track_quat('-Z','Y').to_euler();cam.data.ortho_scale=15;s.render.resolution_x=1500;s.render.resolution_y=1100;s.render.filepath=R+'/previews/SCN_01_Shelter_Detail.png';bpy.ops.render.render(write_still=True)
print('Refinement complete',flush=True)
