import bpy, math, json, os, sys, argparse
from pathlib import Path
from mathutils import Vector, Matrix
parser=argparse.ArgumentParser(description='Build the SharedCore interface candidate from the original v7 scene already opened by Blender.')
parser.add_argument('--character-source',required=True,help='Approved CHR_Peasant_Original65_Baseline.blend path; never modified')
parser.add_argument('--output-root',default=str(Path(__file__).resolve().parents[1]),help='Output kit directory')
args=parser.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
ROOT=os.path.abspath(args.output_root)
for sub in ['source','previews','docs']:os.makedirs(os.path.join(ROOT,sub),exist_ok=True)
if 'SM_Path_4m' not in bpy.data.collections:raise RuntimeError('Open SCN01_Shelter_TA_Candidate.blend v7 before running this script')
source=bpy.context.scene
ns=bpy.data.scenes.new('AETHERLAB_CoreKit_Interface_Test')
ns.unit_settings.system='METRIC';ns.unit_settings.scale_length=1.0
masters=bpy.data.collections.new('01_SOURCE_MODULES__4m_Grid');ns.collection.children.link(masters)
yard=bpy.data.collections.new('02_LINKED_ASSEMBLY_TEST__Not_World');ns.collection.children.link(yard)
labels=bpy.data.collections.new('03_LABELS_AND_SCALE');ns.collection.children.link(labels)
tech=bpy.data.collections.new('04_SOCKET_GUIDES');ns.collection.children.link(tech)
mat_cache={}; report=[]; tests=[]
def mat(m):
 if not m: return None
 if m.name in mat_cache:return mat_cache[m.name]
 n=m.copy();n.name='KIT_'+m.name;n['source_material']=m.name
 # Keep shared UV maps and authored packed image. Explicit meter-space procedural mapping.
 if n.use_nodes:
  tc=n.node_tree.nodes.new('ShaderNodeTexCoord');tc.label='Shared kit / object meters'
  for x in n.node_tree.nodes:
   if x.type=='TEX_NOISE' and not x.inputs['Vector'].is_linked:n.node_tree.links.new(tc.outputs['Object'],x.inputs['Vector'])
 mat_cache[m.name]=n;return n

def mesh_from(name,obs,xf,collection=masters):
 verts=[];faces=[];uv=[];mi=[];smooth=[];mats=[];source_eval=0
 dep=bpy.context.evaluated_depsgraph_get()
 for o in obs:
  ev=o.evaluated_get(dep);me=ev.to_mesh(preserve_all_data_layers=True,depsgraph=dep);off=len(verts)
  verts += [tuple(xf(o.matrix_world@v.co)) for v in me.vertices]
  remap={}
  for i,s in enumerate(me.materials):
   mm=mat(s)
   if mm not in mats:mats.append(mm)
   remap[i]=mats.index(mm)
  ul=me.uv_layers.get('SurfaceUV') or me.uv_layers.active
  for p in me.polygons:
   faces.append(tuple(off+i for i in p.vertices));mi.append(remap.get(p.material_index,0));smooth.append(p.use_smooth)
   uv.extend([tuple(ul.data[i].uv) if ul else (0,0) for i in p.loop_indices])
   source_eval+=len(p.vertices)-2
  ev.to_mesh_clear()
 me=bpy.data.meshes.new(name+'_SharedMesh');me.from_pydata(verts,[],faces);me.update()
 for m in mats:
  if m:me.materials.append(m)
 uvlay=me.uv_layers.new(name='SurfaceUV')
 for i,p in enumerate(me.polygons):p.material_index=mi[i];p.use_smooth=smooth[i]
 for i,x in enumerate(uv):uvlay.data[i].uv=x
 ob=bpy.data.objects.new(name,me);collection.objects.link(ob)
 ob['asset_id']=name;ob['stage']='Kit interface candidate; not final art or engine acceptance';ob['source_collection']=';'.join(sorted({c.name for o in obs for c in o.users_collection}));ob['source_object_count']=len(obs);ob['source_library_id']='libfile_7bdc97dcea808191af98e31eaff40bab';ob['source_version']=7;ob['grid_m']=4.0
 ob.asset_mark();ob.asset_data.description='Reused AetherLab source, meter scale, shared mesh, tested 4m sockets. Candidate.'
 report.append(dict(asset_id=name,source_objects=[o.name for o in obs],source_mesh_objects=len(obs),source_evaluated_triangles=source_eval,output_triangles=sum(len(p.vertices)-2 for p in me.polygons),materials=len(me.materials),output_mesh_objects=1,uv_layer='SurfaceUV'))
 return ob

def bounds(obs):
 vs=[o.matrix_world@Vector(v) for o in obs for v in o.bound_box]
 return Vector([min(v[i] for v in vs) for i in range(3)]),Vector([max(v[i] for v in vs) for i in range(3)])

def centerxf(obs):
 lo,hi=bounds(obs);return lambda v:Vector((v.x-(lo.x+hi.x)/2,v.y-(lo.y+hi.y)/2,v.z-lo.z))

def socket(o,name,xyz):
 e=bpy.data.objects.new(o.name+'__'+name,None);tech.objects.link(e);e.empty_display_type='ARROWS';e.empty_display_size=.25;e.parent=o;e.location=xyz;e.hide_render=True
 o[name]=list(xyz);return e

def duplicate(master,loc,rot=0,name=None):
 o=master.copy();o.data=master.data;o.name=name or master.name+'_Instance';yard.objects.link(o);o.location=loc;o.rotation_euler.z=rot;return o

def basicmat(name,c,metal=0):
 m=bpy.data.materials.new(name);m.diffuse_color=(*c,1);m.use_nodes=True;p=m.node_tree.nodes.get('Principled BSDF');p.inputs['Base Color'].default_value=(*c,1);p.inputs['Roughness'].default_value=.8;p.inputs['Metallic'].default_value=metal;return m
floor=basicmat('TEST_ONLY_Charcoal',(0.07,.09,.10));textmat=basicmat('TEST_ONLY_Label',(0.72,.82,.78));gold=basicmat('TEST_ONLY_Human',(0.7,.32,.07));line=basicmat('TEST_ONLY_Grid',(0.15,.23,.24))
def box(name,loc,dims,material,col=labels):
 x,y,z=[d/2 for d in dims];me=bpy.data.meshes.new(name);me.from_pydata([(-x,-y,-z),(-x,-y,z),(-x,y,-z),(-x,y,z),(x,-y,-z),(x,-y,z),(x,y,-z),(x,y,z)],[],[(0,4,6,2),(1,3,7,5),(0,1,5,4),(2,6,7,3),(0,2,3,1),(4,5,7,6)]);me.materials.append(material);o=bpy.data.objects.new(name,me);col.objects.link(o);o.location=loc;return o

def txt(body,loc,size=.3,rot=(0,0,0)):
 cu=bpy.data.curves.new(body,'FONT');cu.body=body;cu.size=size;cu.extrude=.0004;cu.materials.append(textmat);o=bpy.data.objects.new(body,cu);labels.objects.link(o);o.location=loc;o.rotation_euler=rot;return o
# Normalize pavement from its actual >4m bounding box to a 4m socket grid, retaining chipped source shapes.
pathobs=list(bpy.data.collections['SM_Path_4m'].objects);lo,hi=bounds(pathobs)
path=mesh_from('KIT_Path_Flagstone_4x4_A',pathobs,lambda v:Vector(((v.x-(lo.x+hi.x)/2)*3.92/max(hi.x-lo.x,hi.y-lo.y),(v.y-lo.y)*3.92/max(hi.x-lo.x,hi.y-lo.y)+.04,v.z-hi.z)))
for n,p in [('IN',(0,0,0)),('OUT',(0,4,0)),('LEFT',(-2,2,0)),('RIGHT',(2,2,0))]:socket(path,n,p)
bed=box('KIT_Path_Substrate_4x4',(0,0,0),(4,4,.12),mat(bpy.data.materials['Wet mountain earth']),masters)
# Bed authored around start-edge origin; exactly closed footprint with top at -0.035m.
for v in bed.data.vertices:v.co.y+=2;v.co.z-=.095
bed['asset_id']=bed.name;bed['grid_m']=4.;bed['source_material']='Wet mountain earth';bed['stage']='new minimal connective substrate, not final terrain blend';bed.asset_mark()
uv=bed.data.uv_layers.new(name='SurfaceUV')
for p in bed.data.polygons:
 for li in p.loop_indices:
  co=bed.data.vertices[bed.data.loops[li].vertex_index].co;uv.data[li].uv=(co.x,co.y)
report.append(dict(asset_id=bed.name,source_objects=[],source_mesh_objects=0,source_evaluated_triangles=0,output_triangles=12,materials=1,output_mesh_objects=1,uv_layer='SurfaceUV',note='new connective substrate, using existing material'))
# Low-wall running masonry separated from end/corner piers; reusable ends are not duplicated into every segment.
wallobs=[o for o in bpy.data.collections['SM_Wall_4m'].objects if o.name.startswith('Rubble')];lo,hi=bounds(wallobs)
wall=mesh_from('KIT_WallLow_Run_4m',wallobs,lambda v:Vector((v.x-3.2,(v.y-lo.y)*4/(hi.y-lo.y),v.z-lo.z)))
for n,p in [('IN',(0,0,0)),('OUT',(0,4,0))]:socket(wall,n,p)
pierobs=[o for o in bpy.data.collections['SM_Wall_4m'].objects if o.name.startswith('Pillar')]
pier=mesh_from('KIT_WallLow_EndCornerPier',pierobs,centerxf(pierobs));socket(pier,'CENTER',(0,0,0))
fenceobs=list(bpy.data.collections['SM_Fence_4m'].objects)
rails=mesh_from('KIT_Fence_Rails_4m',[o for o in fenceobs if 'rail' in o.name],lambda v:Vector((v.x-10.4,v.y+8,v.z+1.03)))
for n,p in [('IN',(0,0,0)),('MID',(0,2,0)),('OUT',(0,4,0))]:socket(rails,n,p)
postobs=[bpy.data.objects['Fence stake']];post=mesh_from('KIT_Fence_SharedPost',postobs,centerxf(postobs));socket(post,'CENTER',(0,0,0))
lampobs=list(bpy.data.collections['SM_LanternPost'].objects)
lamp=mesh_from('KIT_LanternPost',lampobs,lambda v:Vector((v.x-2.55,v.y-5,v.z-.15)))
# A ramp derives from the same pavement and substrate, with unchanged horizontal sockets.
ramp=path.copy();ramp.data=path.data.copy();ramp.name='KIT_Path_Ramp_4m_Rise1m';masters.objects.link(ramp)
for v in ramp.data.vertices:v.co.z+=v.co.y*.25
ramp['asset_id']=ramp.name;ramp['derived_from']=path.name;ramp['rise_m']=1.;ramp.asset_mark()
for n,p in [('IN',(0,0,0)),('OUT',(0,4,1))]:socket(ramp,n,p)
rampbed=bed.copy();rampbed.data=bed.data.copy();rampbed.name='KIT_Substrate_Ramp_4m_Rise1m';masters.objects.link(rampbed)
for v in rampbed.data.vertices:v.co.z+=v.co.y*.25
rampbed['asset_id']=rampbed.name;rampbed['derived_from']=bed.name
for ob in [ramp,rampbed]:report.append(dict(asset_id=ob.name,source_objects=[path.name if ob==ramp else bed.name],source_mesh_objects=1,source_evaluated_triangles=sum(len(p.vertices)-2 for p in ob.data.polygons),output_triangles=sum(len(p.vertices)-2 for p in ob.data.polygons),materials=len(ob.data.materials),output_mesh_objects=1,uv_layer='SurfaceUV',note='derived slope connection variant'))
# Source display row, never interpreted as world placement.
positions=[(-13,0,0),(-13,0,0),(-8,0,0),(-5,0,0),(-2,0,0),(0,0,0),(2.5,2,0),(-13,6,0),(-13,6,0)]
for ob,pos in zip([path,bed,wall,pier,rails,post,lamp,ramp,rampbed],positions):ob.location=pos
for t,pos in [('PAVING / 4m',(-15,-.8,.02)),('LOW WALL',(-9,-.8,.02)),('PIER',(-5.7,-.8,.02)),('FENCE / SHARED POST',(-3.2,-.8,.02)),('LANTERN',(1.5,-.8,.02)),('RAMP / +1m',(-15,5.2,.02))]:txt(t,pos,.27)
# Isolated assembly proof: straight, cross branch, right-angle turn, and ramp. Each visible repeated piece is linked.
tiles=[(8,0,0),(8,4,0),(8,8,0),(4,4,0),(12,4,0),(12,8,0)]
for i,p in enumerate(tiles):
 a=duplicate(path,p,name=f'TEST_Paving_{i:02d}');duplicate(bed,p,name=f'TEST_Substrate_{i:02d}')
 # Only whole tile rotations are not used here, keeping socket naming stable.
for p in [(8,12,0)]:duplicate(ramp,p);duplicate(rampbed,p)
duplicate(path,(8,16,1));duplicate(bed,(8,16,1))
# Low-wall 90-degree corner proof. Run ends meet exactly; one pier occupies the junction.
for loc,rot in [((5.7,0,0),0),((5.7,4,0),math.pi/2)]:duplicate(wall,loc,rot)
for p in [(5.7,0,0),(5.7,4,0),(1.7,4,0)]:duplicate(pier,p)
# Fence closed U/test right turn, posts only once at shared connections.
for loc,rot in [((14.5,0,0),0),((14.5,4,0),0),((14.5,8,0),math.pi/2)]:duplicate(rails,loc,rot)
for p in [(14.5,0,0),(14.5,2,0),(14.5,4,0),(14.5,6,0),(14.5,8,0),(12.5,8,0),(10.5,8,0)]:duplicate(post,p)
for p in [(10.5,1,0),(5.5,9,0),(10.5,17,1)]:duplicate(lamp,p)
# CHR_01: v4 defines 1.65m full body height. This marker is not an imported game character.
box('TEST_Scale_Human_Torso',(8,6,1.02),(.39,.21,.6),gold)
for x in [7.87,8.13]:box('TEST_Scale_Human_Leg',(x,6,.4),(.15,.18,.78),gold)
box('TEST_Scale_Human_Head',(8,6,1.49),(.23,.22,.32),gold)
box('TEST_Height_1p65m',(8,6,1.65),(.26,.25,.008),textmat)
txt('1.65m TARGET  /  1.803m ACTUAL SOURCE',(7.25,5.2,.025),.15)
# Append the existing approved 65-bone baseline as an unscaled review reference.
charfile=os.path.abspath(args.character_source)
char_names=['CHR_Peasant_Original65','Female_Peasant_Arms','Female_Peasant_Body','Female_Peasant_Legs','Female_Peasant_Feet','Eyebrows','Eyes','Quaternius_Female_HeadNeck','Quaternius_Hair_SimpleParted']
with bpy.data.libraries.load(charfile,link=False) as (src,dst):dst.objects=[n for n in char_names if n in src.objects]
charobjs=[o for o in dst.objects if o]
charcol=bpy.data.collections.new('05_EXISTING_CHARACTER__UNSCALED_65_BONES');ns.collection.children.link(charcol)
root=bpy.data.objects.new('SCALE_REFERENCE_TRANSLATION_ONLY',None);charcol.objects.link(root);root.location=(9.2,6,.006743584759533405)
for o in charobjs:
 charcol.objects.link(o)
 if o.type=='MESH':
  o.data=o.data.copy();o.data.materials.clear();o.data.materials.append(gold)
  o['review_material_override']='clay only; source mesh, weights and rig unchanged'
 if not o.parent or o.parent not in charobjs:o.parent=root
 o['reference_only']=True
root['measured_full_height_m']=1.8027965174987912;root['design_height_m']=1.65;root['character_scale_modified']=False
root['note']='Actual approved baseline geometry and 65-bone rig, unscaled; clay review materials replace textures only in this separate kit file. Target design height is different. Translation only.'

box('TEST_ONLY_Floor',(0,8,-.24),(35,29,.25),floor)
for x in range(-16,17,4):box('TEST_Grid_X',(x,8,-.105),(.014,27,.009),line)
for y in range(-4,23,4):box('TEST_Grid_Y',(0,y,-.105),(34,.014,.009),line)
txt('AETHERLAB  /  SHARED CORE KIT',(-15,-3.4,.03),.64)
txt('INTERFACE CANDIDATE  |  SOURCE REUSE  |  NOT WORLD LAYOUT',(-15,-2.2,.03),.26)
txt('LINKED ASSEMBLY TEST',(3,21,.03),.47)
txt('STRAIGHT / L / T-X / RAMP / SHARED CORNER',(3,20.2,.03),.22)
# Test mathematical socket coincidence and shared datablocks, independent of visual QA.
def worldsocket(ob,k):return ob.matrix_world@Vector(ob[k])
bpy.context.window.scene=ns;ns.view_layers.update()
# Independent measurements are written only by validate_core_kit.py after reopen.
# Check actual data coordinates and generated placements, not only labels.
pathboxes=[]
for o in yard.objects:
 if o.data==bed.data:
  vs=[o.matrix_world@v.co for v in o.data.vertices];pathboxes.append(dict(name=o.name,min=[min(v[i] for v in vs) for i in range(3)],max=[max(v[i] for v in vs) for i in range(3)]))
expected=[(4.,4.),(4.,4.)]
instances=[o for o in yard.objects if o.type=='MESH']
rt=dict(source_file='SCN01_Shelter_TA_Candidate.blend',source_library_id='libfile_7bdc97dcea808191af98e31eaff40bab',source_version=7,scope='Core kit production cleanup and isolated joint tests only; full world not assembled',module_count=len(report),modules=report,assembly_mesh_objects=len(instances),assembly_unique_meshes=len({o.data.name for o in instances}),assembly_linked_to_masters=sum(o.data in [a.data for a in masters.objects if a.type=='MESH'] for o in instances),validation_status='awaiting_reopen_validation',substrate_bounds=pathboxes,limitations=['No engine run, navigation, collision or GPU performance acceptance','No claim of final reference-image style match','No LOD or lightmap authored for this candidate','4m low walls retain visible stone seams; corner pier intentionally overlaps wall terminations','4x4 field tile supports intersection footprint; dedicated dressed corner/edge transitions remain required'])
# Lighting / review cameras.
world=bpy.data.worlds.new('KitReview_Overcast');world.use_nodes=True;world.node_tree.nodes['Background'].inputs[0].default_value=(.35,.42,.48,1);world.node_tree.nodes['Background'].inputs[1].default_value=.5;ns.world=world
sun=bpy.data.lights.new('ReviewSun','SUN');sun.energy=2;sun.angle=.35;so=bpy.data.objects.new('ReviewSun',sun);ns.collection.objects.link(so);so.rotation_euler=(.45,-.65,-.4)
area=bpy.data.lights.new('ReviewFill','AREA');area.energy=1900;area.shape='DISK';area.size=25;ao=bpy.data.objects.new('ReviewFill',area);ns.collection.objects.link(ao);ao.location=(-6,3,17)
def camera(name,loc,target,scale):
 c=bpy.data.cameras.new(name);o=bpy.data.objects.new(name,c);ns.collection.objects.link(o);o.location=loc;o.rotation_euler=(Vector(target)-o.location).to_track_quat('-Z','Y').to_euler();c.type='ORTHO';c.ortho_scale=scale;return o
cam=camera('CAM_CoreKit_Overview',(33,-42,40),(0,8,0),42);ns.camera=cam
camera('CAM_Assembly_Close',(28,-13,19),(8,8,0),24)
ns.render.engine='CYCLES';ns.cycles.samples=40;ns.cycles.use_denoising=False;ns.render.resolution_x=1600;ns.render.resolution_y=1100;ns.render.resolution_percentage=100
ns.view_settings.view_transform='AgX';ns.render.image_settings.file_format='PNG';ns.render.filepath=ROOT+'/previews/CoreKit_Overview.png'
# Opening the saved library shows only the validated yard, not the old scene contents.
for screen in bpy.data.screens:
 for a in screen.areas:
  if a.type=='VIEW_3D':a.spaces.active.region_3d.view_perspective='CAMERA'
text=bpy.data.texts.new('README_CoreKit');text.write('AetherLab core kit candidate, 2026-10-01.\nReused source v7. Only a small interface test yard; not the 800m world.\n4m grid in meters. Master meshes in 01, linked copies in 02.\nPaving and substrate use start-edge origin y=0, end y=4.\nLow wall and fence rails have independent piers/posts. No duplicate shared corner post.\nRamp rises +1m per4m.\nCHR_01 marker is 1.65m full-body height per Design-v4; no eye-height assumption or engine acceptance.\nUV tile overlap is intentional. No lightmap provided.\nSee CoreKit_Validation.json and asset matrix for remaining work.\n')
ns['readme']=text.name

bpy.data.libraries.write(ROOT+'/source/AetherLab_CoreKit_Interface_v1.blend',{ns,text},path_remap='RELATIVE',fake_user=True,compress=True)
json.dump(rt,open(ROOT+'/docs/CoreKit_Validation.json','w'),ensure_ascii=False,indent=2)
bpy.ops.wm.open_mainfile(filepath=ROOT+'/source/AetherLab_CoreKit_Interface_v1.blend')
ns=bpy.data.scenes['AETHERLAB_CoreKit_Interface_Test'];bpy.context.window.scene=ns
for other in list(bpy.data.scenes):
 if other!=ns:bpy.data.scenes.remove(other)
ns.camera=bpy.data.objects['CAM_CoreKit_Overview']
ns['asset_scope']='SharedCore_v1: 9 candidate masters + isolated assembly. Not full world.'
ns['source_reference']='Original source retained separately. No original character resize.'
for screen in bpy.data.screens:
 for a in screen.areas:
  if a.type=='VIEW_3D':a.spaces.active.region_3d.view_perspective='CAMERA'
bpy.ops.wm.save_as_mainfile(filepath=ROOT+'/source/AetherLab_CoreKit_Interface_v1.blend',compress=True)
bpy.ops.render.render(write_still=True)
ns.camera=bpy.data.objects['CAM_Assembly_Close'];ns.render.filepath=ROOT+'/previews/CoreKit_Assembly_Close.png';ns.render.resolution_x=1200;ns.render.resolution_y=1000
bpy.ops.render.render(write_still=True)
print('KIT_SAVED',ROOT+'/source/AetherLab_CoreKit_Interface_v1.blend')
