"""KITE-01: original nonfunctional game prop. Blender 4.3+, meters, +X forward.
No real weapon internals, mechanical interfaces or manufacturing specification.
"""
import bpy, math, json, sys, hashlib
from pathlib import Path
from mathutils import Vector
import numpy as np
ROOT=Path(__file__).resolve().parents[1]
for n in ['textures','exports','renders','docs']: (ROOT/n).mkdir(exist_ok=True)
bpy.ops.object.select_all(action='SELECT'); bpy.ops.object.delete(use_global=False)
scene=bpy.context.scene; scene.unit_settings.system='METRIC'; scene.unit_settings.scale_length=1
scene.render.engine='CYCLES'; scene.cycles.samples=32; scene.cycles.use_denoising=False
scene.render.resolution_x=1600; scene.render.resolution_y=1000; scene.render.resolution_percentage=100
scene.world.color=(.22,.22,.22)
scene.view_settings.view_transform='AgX'
modules={}; current=None
# Tileable authored micro-surface maps, not third-party textures. Metal/roughness are explicit.
def material(name,col,metal,rough):
 m=bpy.data.materials.new(name); m.use_nodes=True; ns=m.node_tree.nodes; ls=m.node_tree.links; bs=ns.get('Principled BSDF'); bs.inputs['Metallic'].default_value=metal
 rng=np.random.default_rng(73); n=512; grain=rng.normal(0,.014,(n,n,1)); rgb=np.clip(np.array(col)[None,None,:]*(1+grain*2),0,1)
 arr=np.concatenate([rgb,np.ones((n,n,1))],axis=2).astype('float32'); im=bpy.data.images.new(name+'_BaseColor',width=n,height=n); im.pixels.foreach_set(arr.ravel()); im.filepath_raw=str(ROOT/'textures'/f'{name}_BaseColor.png'); im.file_format='PNG'; im.save(); im.pack()
 t=ns.new('ShaderNodeTexImage'); t.image=im; ls.new(t.outputs['Color'],bs.inputs['Base Color'])
 arr=np.ones((n,n,4),dtype='float32'); arr[:,:,:3]=np.clip(rough+grain,0,1)
 im=bpy.data.images.new(name+'_Roughness',width=n,height=n); im.colorspace_settings.name='Non-Color'; im.pixels.foreach_set(arr.ravel()); im.filepath_raw=str(ROOT/'textures'/f'{name}_Roughness.png'); im.file_format='PNG'; im.save(); im.pack()
 t=ns.new('ShaderNodeTexImage'); t.image=im; ls.new(t.outputs['Color'],bs.inputs['Roughness'])
 return m
sand=material('M_CeramicSand',(.38,.31,.215),.3,.48); dark=material('M_Graphite',(.052,.066,.071),.8,.36); rubber=material('M_Polymer',(.022,.026,.027),0,.72); steel=material('M_EdgeMetal',(.17,.19,.20),.9,.32); teal=material('M_InventoryTeal',(.035,.24,.23),.3,.42); glass=material('M_OpticLens',(.04,.27,.3),.65,.16)
def reg(o,mat):
 o.data.materials.append(mat); modules[current].append(o); return o
def bevel(o,w=.003,seg=3):
 bpy.context.view_layer.objects.active=o
 mod=o.modifiers.new('Authored edge bevel','BEVEL'); mod.width=w; mod.segments=seg
 bpy.ops.object.modifier_apply(modifier=mod.name)
 mod=o.modifiers.new('Weighted corner normals','WEIGHTED_NORMAL'); mod.keep_sharp=True; bpy.ops.object.modifier_apply(modifier=mod.name)
 return o
def box(name,loc,dim,mat,bev=.002,rot=0):
 bpy.ops.mesh.primitive_cube_add(size=1,location=loc); o=bpy.context.object; o.name=name; o.dimensions=dim; bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
 if bev: bevel(o,bev)
 o.rotation_euler.y=rot; return reg(o,mat)
def profile(name,pts,depth,mat,bev=.002,y=0):
 vs=[(x,y+s*depth/2,z) for s in [-1,1] for x,z in pts]; n=len(pts); fs=[tuple(reversed(range(n))),tuple(range(n,2*n))]+[(i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n)]
 me=bpy.data.meshes.new(name); me.from_pydata(vs,[],fs); me.update(); o=bpy.data.objects.new(name,me); scene.collection.objects.link(o)
 bpy.context.view_layer.objects.active=o; o.select_set(True)
 if bev: bevel(o,bev)
 return reg(o,mat)
def cyl(name,loc,r,depth,mat,axis='X',vertices=24):
 bpy.ops.mesh.primitive_cylinder_add(vertices=vertices,radius=r,depth=depth,location=loc); o=bpy.context.object; o.name=name
 if axis=='X': o.rotation_euler.y=math.pi/2
 if axis=='Y': o.rotation_euler.x=math.pi/2
 bpy.ops.object.transform_apply(location=False,rotation=True,scale=True); bevel(o,.0008,2); return reg(o,mat)
def beam(name,a,b,width,depth,mat):
 a=Vector(a); b=Vector(b); o=box(name,(a+b)/2,(width,depth,(b-a).length),mat); o.rotation_euler=(b-a).to_track_quat('Z','Y').to_euler(); return o
def start(name):
 global current; current=name; modules[name]=[]
start('Receiver')
profile('Upper shell',[(-.13,.035),(-.13,.102),(-.10,.124),(.11,.124),(.15,.102),(.15,.018),(.06,.007),(-.075,.008)],.057,dark)
profile('Lower shell',[(-.125,.032),(.11,.032),(.11,-.032),(.027,-.042),(-.012,-.013),(-.08,-.013)],.05,dark)
for s in [-1,1]:
 profile('Raised flank panel',[(-.102,.056),(-.091,.1),(.1,.1),(.12,.082),(.045,.065)],.003,steel,.001,y=s*.030)
 profile('Inventory stripe',[(-.022,.036),(.002,.036),(.04,.096),(.018,.096)],.001,teal,.0002,y=s*.032)
 for x,z in [(-.10,.105),(.11,.105),(.09,.018),(-.07,.022)]: cyl('Fastener visual cap',(x,s*.032,z),.004,.002,rubber,'Y',12)
box('Top sight base',(.13,0,.129),(.50,.025,.012),dark)
for i in range(30): box('Rail ridge',(-.1+i*.016,0,.139),(.008,.035,.008),steel,.001)
# exterior trigger guard: open window with independent trigger, no working internals
beam('Guard front',(-.005,0,-.015),(-.005,0,-.073),.009,.021,dark)
beam('Guard bottom',(-.005,0,-.073),(-.077,0,-.073),.009,.021,dark)
start('Handguard_Sand')
profile('Handguard spine',[(.15,.114),(.43,.114),(.444,.094),(.437,.021),(.17,.013),(.15,.032)],.056,sand)
for s in [-1,1]:
 # inset dark lozenges and ceramic bridges: intentionally shallow exterior detail, not functional vents
 for x in [.20,.285,.37]:
  box('Recessed vent shadow',(x,s*.029,.073),(.064,.003,.018),rubber,.006)
  box('Recess highlight',(x,s*.031,.086),(.055,.002,.002),steel,.0005)
 for x in [.16,.423]: cyl('Furniture cap',(x,s*.03,.031),.004,.002,dark,'Y',12)
box('Lower support pad',(.31,0,.013),(.17,.049,.017),rubber,.005)
for i in range(9): box('Support pad rib',(.235+i*.018,0,.002),(.008,.051,.004),dark,.001)
start('Muzzle_Short')
cyl('Sealed decorative neck',(.465,0,.079),.018,.055,steel)
cyl('Decorative shroud',(.510,0,.079),.024,.054,dark)
cyl('Nonfunctional front face',(.538,0,.079),.016,.001,rubber)
for i in range(8):
 a=i*math.tau/8; o=box('Shroud relief',(.51,math.sin(a)*.023,.079+math.cos(a)*.023),(.035,.008,.003),steel,.001); o.rotation_euler.x=-a
start('Stock_Skeleton')
cyl('Stock collar',(-.15,0,.076),.028,.035,steel)
beam('Upper stock spar',(-.17,0,.09),(-.365,0,.078),.029,.043,sand)
beam('Lower stock spar',(-.18,0,.051),(-.36,0,-.044),.025,.034,sand)
beam('Rear stock spar',(-.365,0,.078),(-.36,0,-.054),.028,.039,sand)
box('Cheek pad',(-.275,0,.108),(.15,.045,.023),rubber,.006)
box('Shoulder rubber pad',(-.388,0,.015),(.026,.051,.167),rubber,.006,rot=-.05)
for z in np.linspace(-.052,.074,10): box('Butt pad tread',(-.404,0,float(z)),(.004,.046,.005),dark,.001)
start('Grip_Angled')
profile('Grip body',[(-.119,.005),(-.073,-.004),(-.091,-.049),(-.132,-.15),(-.174,-.131)],.033,rubber,.006)
for s in [-1,1]:
 profile('Grip insert',[(-.109,-.041),(-.098,-.054),(-.132,-.131),(-.157,-.12)],.002,dark,.002,y=s*.017)
 for i in range(8): box('Grip tactile rib',(-.117-i*.004,s*.018,-.055-i*.009),(.024,.002,.0025),steel,.0005,rot=.38)
start('Magazine_Box')
profile('Magazine body',[(.027,-.025),(.10,-.025),(.083,-.19),(.004,-.182)],.042,dark,.004)
box('Magazine floorplate',(.043,0,-.191),(.092,.049,.016),rubber,.003,rot=-.10)
for s in [-1,1]:
 for x in [.028,.065]: box('Magazine flute',(x,s*.022,-.113),(.006,.003,.123),steel,.001,rot=.10)
 for z in [-.065,-.12,-.166]: box('Magazine cross seam',(.045,s*.024,z),(.065,.002,.003),rubber,.0005)
start('Optic_Reflex')
box('Optic foot',(-.028,0,.154),(.092,.049,.02),rubber)
profile('Optic body',[(-.064,.162),(-.057,.211),(-.034,.23),(-.005,.225),(.018,.165)],.041,dark)
box('Lens visual face',(.013,0,.195),(.003,.029,.038),glass,.002,rot=-.4)
for s in [-1,1]: cyl('Optic dial',(-.033,s*.025,.179),.01,.013,steel,'Y')
start('ChargingHandle')
box('Exterior sliding tab',(-.07,-.042,.086),(.031,.03,.012),steel,.003)
start('Trigger')
profile('Decorative trigger',[(-.034,-.018),(-.024,-.019),(-.025,-.045),(-.039,-.057),(-.044,-.053),(-.034,-.038)],.008,steel,.001)
# Join by swappable module, bake transforms, unique UV0 per module, standardized pivot interfaces.
pivots={'Receiver':(0,0,0),'Handguard_Sand':(.15,0,.079),'Muzzle_Short':(.445,0,.079),'Stock_Skeleton':(-.13,0,.076),'Grip_Angled':(-.105,0,-.005),'Magazine_Box':(.064,0,-.026),'Optic_Reflex':(-.028,0,.145),'ChargingHandle':(-.07,-.027,.086),'Trigger':(-.03,0,-.018)}
parts=[]
for name,objs in modules.items():
 bpy.ops.object.select_all(action='DESELECT')
 for o in objs:o.select_set(True)
 bpy.context.view_layer.objects.active=objs[0]; bpy.ops.object.join(); o=bpy.context.object; o.name='SM_KITE01_'+name
 bpy.ops.object.transform_apply(location=False,rotation=True,scale=True)
 scene.cursor.location=pivots[name]; bpy.ops.object.origin_set(type='ORIGIN_CURSOR')
 bpy.ops.object.mode_set(mode='EDIT'); bpy.ops.mesh.select_all(action='SELECT'); bpy.ops.uv.smart_project(island_margin=.018); bpy.ops.object.mode_set(mode='OBJECT')
 o.data.uv_layers.active.name='UV0_Surface'; o['module_id']=name; o['role']='animated' if name in ['Magazine_Box','ChargingHandle','Trigger'] else 'cosmetic_interchangeable'; parts.append(o)
# Real alternate cosmetic stock and muzzle assets, housed in separate variant collection.
variant=bpy.data.collections.new('VARIANTS_hidden'); scene.collection.children.link(variant)
start('Stock_Compact')
box('Compact cosmetic stock',(-.26,0,.059),(.20,.041,.06),sand,.008)
box('Compact shoulder pad',(-.365,0,.032),(.025,.05,.11),rubber,.005)
start('Muzzle_Cover')
cyl('Long decorative cover',(.495,0,.079),.026,.10,sand); cyl('Sealed cover face',(.546,0,.079),.017,.001,rubber)
for name in ['Stock_Compact','Muzzle_Cover']:
 objs=modules[name]; bpy.ops.object.select_all(action='DESELECT')
 for o in objs:o.select_set(True)
 bpy.context.view_layer.objects.active=objs[0]; bpy.ops.object.join(); o=bpy.context.object; o.name='SM_KITE01_'+name
 scene.cursor.location=pivots['Stock_Skeleton' if name=='Stock_Compact' else 'Muzzle_Short']; bpy.ops.object.origin_set(type='ORIGIN_CURSOR')
 bpy.ops.object.transform_apply(location=False,rotation=True,scale=True); bpy.ops.object.mode_set(mode='EDIT'); bpy.ops.mesh.select_all(action='SELECT'); bpy.ops.uv.smart_project(island_margin=.018); bpy.ops.object.mode_set(mode='OBJECT')
 for c in list(o.users_collection): c.objects.unlink(o)
 variant.objects.link(o); o['module_id']=name; o.hide_render=True; parts.append(o)
root=bpy.data.objects.new('KITE01_Root',None); scene.collection.objects.link(root)
for o in parts:o.parent=root
sockets={
 'Grip_R':(-.128,0,-.082),'Support_L':(.30,0,.012),'Magazine':(.064,0,-.026),'Optic':(-.028,0,.145),'Stock':(-.13,0,.076),'Muzzle':(.445,0,.079),'Aim':(.54,0,.199),'Reload_Clearance':(.064,0,-.32)}
for n,p in sockets.items():
 o=bpy.data.objects.new('SOCKET_KITE01_'+n,None); scene.collection.objects.link(o); o.location=p; o.empty_display_type='ARROWS'; o.empty_display_size=.035; o.parent=root
# Illustrative object-only animation, no claim of completed character reload.
mag=bpy.data.objects['SM_KITE01_Magazine_Box']; neutral=mag.location.copy()
for f,dz,dx in [(1,0,0),(12,0,0),(30,-.23,0),(45,-.23,-.08),(60,-.23,0),(80,0,0),(96,0,0)]:
 mag.location=neutral+Vector((dx,0,dz)); mag.keyframe_insert(data_path='location',frame=f)
mag.animation_data.action.name='KITE01_MagazineSwap_Study'
handle=bpy.data.objects['SM_KITE01_ChargingHandle']; neutral=handle.location.copy()
for f,x in [(1,0),(80,0),(86,-.035),(92,0),(96,0)]:handle.location=neutral+Vector((x,0,0));handle.keyframe_insert(data_path='location',frame=f)
handle.animation_data.action.name='KITE01_ExteriorHandle_Study'; scene.frame_end=96; scene.render.fps=24; scene.frame_set(1)
# LOD copies retain module origins and UVs.
lods={0:[o for o in parts if o.name not in ['SM_KITE01_Stock_Compact','SM_KITE01_Muzzle_Cover']]}
for level,ratio in [(1,.48),(2,.20)]:
 col=bpy.data.collections.new(f'LOD{level}_hidden');scene.collection.children.link(col);lods[level]=[]
 for orig in lods[0]:
  o=orig.copy();o.data=orig.data.copy();o.animation_data_clear();col.objects.link(o);o.name=orig.name+f'_LOD{level}';bpy.context.view_layer.objects.active=o
  m=o.modifiers.new('Reduction','DECIMATE');m.ratio=ratio;bpy.ops.object.modifier_apply(modifier=m.name);o.hide_render=True;lods[level].append(o)
# Export each LOD assembly. LOD0 has moving components and sockets, variants independently exportable.
for level,objects in lods.items():
 bpy.ops.object.select_all(action='DESELECT')
 for o in objects:o.select_set(True)
 root.select_set(True)
 if level==0:
  for o in root.children:
   if o.type=='EMPTY':o.select_set(True)
 bpy.ops.export_scene.gltf(filepath=str(ROOT/'exports'/f'KITE01_LOD{level}.glb'),export_format='GLB',use_selection=True,export_animations=level==0,export_extras=True)
# Export modular FBX individually at each local interface origin; sockets are in JSON/GLB, not ambiguous multi-mesh FBX.
for o in parts:
 bpy.ops.object.select_all(action='DESELECT');o.select_set(True); old=o.location.copy();o.location=(0,0,0)
 bpy.ops.export_scene.fbx(filepath=str(ROOT/'exports'/f'{o.name}.fbx'),use_selection=True,object_types={'MESH'},bake_anim=False,add_leaf_bones=False,axis_forward='-Y',axis_up='Z',path_mode='COPY',embed_textures=True)
 o.location=old
# Studio camera and lighting, excluded from exports.
for name,loc,power,size in [('Key',(.2,-.7,1.3),200,1.1),('Fill',(-.4,.5,.7),130,1),('Rim',(.5,.6,.4),90,.5)]:
 data=bpy.data.lights.new(name,'AREA');data.energy=power;data.shape='DISK';data.size=size;o=bpy.data.objects.new(name,data);scene.collection.objects.link(o);o.location=loc;o.rotation_euler=(Vector((.07,0,.02))-o.location).to_track_quat('-Z','Y').to_euler()
bpy.ops.object.camera_add(location=(1.07,-1.5,.75));camera=bpy.context.object;camera.name='CAM_Beauty';camera.rotation_euler=(Vector((.07,0,.015))-camera.location).to_track_quat('-Z','Y').to_euler();camera.data.type='ORTHO';camera.data.ortho_scale=1.16;scene.camera=camera
bpy.ops.mesh.primitive_plane_add(size=200,location=(0,0,-.225));floor=bpy.context.object;floor.name='STUDIO_Floor';floor.data.materials.append(material('M_Studio',(.12,.15,.17),0,.9))
# Save native file with hidden LOD/variant collections in viewport.
variant.hide_viewport=True
for c in bpy.data.collections:
 if c.name.startswith('LOD'):c.hide_viewport=True
report={'asset':'KITE-01','blender':bpy.app.version_string,'units':'meters','forward':'+X','up':'+Z','character_source':'FrozenCharacter.json','character_pose_validation':'PENDING_TRUE_CHR01_SOURCE','engine_validation':'NOT_RUN_CANCELLED','parts':[],'sockets':sockets,'lod_triangles':{}}
for o in parts:
 o.data.calc_loop_triangles();report['parts'].append({'name':o.name,'triangles':len(o.data.loop_triangles),'vertices':len(o.data.vertices),'uv_layers':[x.name for x in o.data.uv_layers],'pivot':list(o.location),'role':o.get('role','cosmetic_variant')})
for level,objs in lods.items():
 for o in objs:o.data.calc_loop_triangles()
 report['lod_triangles'][str(level)]=sum(len(o.data.loop_triangles) for o in objs)
(ROOT/'docs'/'Build_Manifest.json').write_text(json.dumps(report,indent=2))
bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'KITE01_Modular.blend'))
scene.render.filepath=str(ROOT/'renders'/'KITE01_Beauty.png');bpy.ops.render.render(write_still=True)
camera.location=(.07,-2,.06);camera.rotation_euler=(Vector((.07,0,.02))-camera.location).to_track_quat('-Z','Y').to_euler();camera.data.ortho_scale=1.08
scene.render.filepath=str(ROOT/'renders'/'KITE01_Side.png');bpy.ops.render.render(write_still=True)
print('KITE01_BUILD_COMPLETE',json.dumps(report['lod_triangles']))
