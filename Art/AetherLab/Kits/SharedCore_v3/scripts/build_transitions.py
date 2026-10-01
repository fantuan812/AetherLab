"""SharedCore v3: minimum stair/bridge-bank/rock-ground transition fixtures.
Run in Blender with preserved v2 input; use --source-scene PATH --output-root PATH.
All new dimensions are this fixture's candidates, not a universal grid or structural claim.
"""
import bpy, bmesh, math, json, os, sys, argparse, hashlib
from mathutils import Vector,Matrix
p=argparse.ArgumentParser();p.add_argument('--source-scene',required=True);p.add_argument('--output-root',required=True);a=p.parse_args(sys.argv[sys.argv.index('--')+1:]);ROOT=os.path.abspath(a.output_root)
for d in ['source','docs','previews']:os.makedirs(ROOT+'/'+d,exist_ok=True)
s=bpy.context.scene;s.name='AETHERLAB_CoreKit_Transitions_v3';s.unit_settings.system='METRIC';s.unit_settings.scale_length=1
input_path=bpy.data.filepath
# v2 fixture data stays intact, but is excluded from this v3 evidence view.
for name in ['02_CONNECTION_FIXTURES__NOT_WORLD','03_LABELS_AND_SCALE','04_SOCKET_GUIDES']:
 c=bpy.data.collections[name];c.hide_render=True;c.hide_viewport=True
old=bpy.data.collections['01_MASTER_MODULES__4m_Candidate'];base={o.name:o for o in old.objects if o.type=='MESH'}
masters=bpy.data.collections.new('05_TRANSITION_MASTERS__CANDIDATES');s.collection.children.link(masters)
yard=bpy.data.collections.new('06_CONNECTED_TRANSITION_FIXTURE__NOT_WORLD');s.collection.children.link(yard)
context=bpy.data.collections.new('07_TRANSITION_REVIEW_CONTEXT');s.collection.children.link(context)
stone=bpy.data.materials['KIT_Wet basalt 1'];earth=bpy.data.materials['KIT_Wet mountain earth'];timber=bpy.data.materials['KIT_Aged cedar'];new=[];rows=[]
source_names=['H01_Door_stone_step','SHR_Workbench_top_plank','SHR_Tie_beam','River rock']
with bpy.data.libraries.load(os.path.abspath(a.source_scene),link=False) as (fr,to):to.objects=source_names
sources={o.name:o for o in to.objects};source_record=[]
for o in sources.values():
 source_record.append({'object':o.name,'vertices':len(o.data.vertices),'source_materials':[m.name for m in o.data.materials]})

def finalize(o,name,role,source,sockets):
 o.name=name;o.data.name=name+'_Mesh';o.matrix_world=Matrix.Identity(4);masters.objects.link(o);o['asset_id']=name;o['role']=role;o['derived_from']=source;o['stage']='minimum transition candidate v3';o['local_sockets_json']=json.dumps(sockets);o['allowed_instance_scale']='1,1,1';o.asset_mark();o.hide_render=True;o.hide_set(True)
 for mod in list(o.modifiers):o.modifiers.remove(mod)
 bm=bmesh.new();bm.from_mesh(o.data);bmesh.ops.recalc_face_normals(bm,faces=bm.faces);bm.to_mesh(o.data);bm.free();o.data.update()
 uv=o.data.uv_layers.get('SurfaceUV') or o.data.uv_layers.new(name='SurfaceUV')
 for f in o.data.polygons:
  ax=max(range(3),key=lambda k:abs(f.normal[k]));uvax=[k for k in range(3) if k!=ax]
  for li in f.loop_indices:
   v=o.data.vertices[o.data.loops[li].vertex_index].co;uv.data[li].uv=(v[uvax[0]],v[uvax[1]])
 new.append(o);return o

def author(name,vs,fs,mat,role,source,sockets):
 me=bpy.data.meshes.new(name);me.from_pydata(vs,[],fs);me.materials.append(mat);me.update();return finalize(bpy.data.objects.new(name,me),name,role,source,sockets)

def box_from(src,name,lo,hi,mat,role,sockets):
 o=src.copy();o.data=src.data.copy();vs=[v.co.copy() for v in o.data.vertices];mi=[min(v[k] for v in vs) for k in range(3)];ma=[max(v[k] for v in vs) for k in range(3)]
 for v in o.data.vertices:
  for k in range(3):v.co[k]=lo[k]+(v.co[k]-mi[k])/(ma[k]-mi[k])*(hi[k]-lo[k])
 o.data.materials.clear();o.data.materials.append(mat);return finalize(o,name,role,src.name+' topology; authored dimensions; existing shared material',sockets)

# One continuous solid staircase: 8 x 0.125m rise and 0.5m run; existing door-step source informs the stone construction.
# Side-profile extrusion is necessary connection topology, unlike copying a whole building.
profile=[(0,-.55),(4,-.55),(4,.965)]
for k in range(7,-1,-1):
 profile.append((k*.5,(k+1)*.125-.035))
 if k:profile.append((k*.5,k*.125-.035))
vs=[(x,y,z) for x in [-2,2] for y,z in profile];n=len(profile);fs=[tuple(reversed(range(n))),tuple(range(n,2*n))]+[(i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n)]
stair=author('KIT_Stair_Solid_4m_Rise1m',vs,fs,stone,'stair','H01_Door_stone_step construction; new closed profile',{'IN':[0,0,-.035],'OUT':[0,4,.965]})
plank=box_from(sources['SHR_Workbench_top_plank'],'KIT_Bridge_DeckPlank_4x0p25',(-2,0,-.165),(2,.25,-.035),timber,'bridge_deck',{'IN':[0,0,-.035],'OUT':[0,.25,-.035]})
beam=box_from(sources['SHR_Tie_beam'],'KIT_Bridge_Bearer_4p5m',(-.16,-.25,-.55),(.16,4.25,-.165),timber,'bridge_bearer',{'IN':[0,0,-.55],'OUT':[0,4,-.55]})
abut=box_from(sources['H01_Door_stone_step'],'KIT_Bridge_Abutment_6x1m',(-3,0,-1.65),(3,1,-.035),stone,'bridge_abutment',{'IN':[0,0,-.035],'OUT':[0,1,-.035]})
# Reuse old shoulder topology as a wider/longer collar. Smooth slope is earth, not a rock-shaped collision object.
collar=base['KIT_Shoulder_Straight_1m'].copy();collar.data=base['KIT_Shoulder_Straight_1m'].data.copy()
for v in collar.data.vertices:v.co.x*=2;v.co.y*=2;v.co.z-=.35 if v.co.z<-.54 else 0
collar=finalize(collar,'KIT_RockToe_Earth_2m','rock_toe','KIT_Shoulder_Straight_1m topology and shared earth',{'ROAD_EDGE':[0,1,-.035],'TERRAIN_EDGE':[2,1,-.35]})
rock=sources['River rock'].copy();rock.data=sources['River rock'].data.copy();raw=[sources['River rock'].matrix_world@v.co for v in rock.data.vertices];mi=[min(v[k] for v in raw) for k in range(3)];ma=[max(v[k] for v in raw) for k in range(3)]
for v,w in zip(rock.data.vertices,raw):v.co=(w.x-(mi[0]+ma[0])/2,w.y-mi[1],w.z-mi[2]-.65)
rock.data.materials.clear();rock.data.materials.append(stone)
rock=finalize(rock,'KIT_Rock_River_A','rock','REFINED_Valley/River rock exact source topology, baked original world rotation/scale; translated to embedded datum; shared basalt',{'BASE':[0,0,-.65]})
for o in sources.values():bpy.data.objects.remove(o,do_unlink=True)
# Purge only unused imported material datablocks, never replace existing shared materials.
for m in list(bpy.data.materials):
 if m.users==0:bpy.data.materials.remove(m)
bpy.data.orphans_purge(do_recursive=True)

def inst(m,name,pos=(0,0,0),rot=0,role=None):
 o=m.copy();o.data=m.data;o.name=name;yard.objects.link(o);o.location=pos;o.rotation_euler=(0,0,rot);o.scale=(1,1,1);o.hide_render=False;o.hide_set(False);o['master']=m.name;o['fixture']='TRANSITION_CHAIN';o['role']=role or m.get('role','');return o

def road(name,y,z,raised=False):
 inst(base['KIT_Path_Flagstone_4x4_A'],name+'_Paving',(0,y,z),role='road_surface')
 inst(base['KIT_Path_Substrate_4x4'] if raised else base['KIT_Path_Foundation_4x4'],name+'_Bed',(0,y,z),role='road_support')
 if raised:inst(base['KIT_Raised_Landing_Bank_4m_Height1m'],name+'_Bank',(0,y,z-1),role='ground')
 else:
  for yy in range(y,y+4):
   inst(base['KIT_Shoulder_Straight_1m'],name+f'_ShoulderR{yy}',(2,yy,z),role='ground');inst(base['KIT_Shoulder_Straight_1m'],name+f'_ShoulderL{yy}',(-2,yy+1,z),math.pi,role='ground')
road('LOW_A',-8,0);road('LOW_B',-4,0)
inst(stair,'STAIR_8Risers',(0,0,0));inst(base['KIT_Ramp_Bank_4m_Rise1m'],'STAIR_SideBank',(0,0,0),role='ground')
road('HIGH_APPROACH',4,1,True)
inst(abut,'BRIDGE_NearBank',(0,8,1));inst(abut,'BRIDGE_FarBank',(0,13,1))
for k in range(16):inst(plank,f'BRIDGE_Deck{k:02d}',(0,9+k*.25,1))
for x in [-1.75,1.75]:inst(beam,f'BRIDGE_Bearer{x}',(x,9,1))
road('HIGH_EXIT',14,1,True)
inst(base['KIT_Path_Ramp_4m_Rise1m'],'RETURN_RampPaving',(0,22,0),math.pi,role='road_surface');inst(base['KIT_Substrate_Ramp_4m_Rise1m'],'RETURN_RampBed',(0,22,0),math.pi,role='road_support');inst(base['KIT_Ramp_Bank_4m_Rise1m'],'RETURN_RampBank',(0,22,0),math.pi,role='ground')
road('LOW_EXIT',22,0)
# Rock/soil collar replaces shoulders in a bounded 4m section; two linked copies plus exact-scale source rocks.
for o in list(yard.objects):
 if o.name.startswith('LOW_A_ShoulderR'):bpy.data.objects.remove(o,do_unlink=True)
for k in range(2):
 inst(collar,f'ROCK_Toe{k}',(2,-8+2*k,0));inst(rock,f'ROCK_Source{k}',(3.0,-7.8+2*k,0))
# Rail sections across steps and bridge only, reusing v2 slope + upright masters.
for x in [-1.8,1.8]:
 inst(base['KIT_Fence_Rails_Rise_4m_1m'],f'STAIR_Rail{x}',(x,0,0),role='rail')
 for y,z in [(0,0),(2,.5),(4,1)]:inst(base['KIT_Fence_SharedPost'],f'STAIR_Post{x}_{y}',(x,y,z-.14),role='post')
 inst(base['KIT_Fence_Rails_4m'],f'BRIDGE_Rail{x}',(x,9,1),role='rail')
 for y in [9,11,13]:inst(base['KIT_Fence_SharedPost'],f'BRIDGE_Post{x}_{y}',(x,y,.86),role='post')
# Endpoint bridge rail posts straddle abutment and deck with continuous support on both sides.
# Review water is a separate existing earth-material proxy, excluded from all backing/walkable tests.
me=bpy.data.meshes.new('REVIEW_WaterLevel');me.from_pydata([(-5,9,-.05),(5,9,-.05),(5,13,-.05),(-5,13,-.05)],[],[(0,1,2,3)]);me.materials.append(bpy.data.materials['TEST_ONLY_Charcoal']);ob=bpy.data.objects.new('REVIEW_WATER_LEVEL__NotGameplay',me);context.objects.link(ob);ob['role']='review_water_proxy'
# Unchanged legacy body placed next to 1.65m target ruler. Translation only; no new hero is implied.
root=bpy.data.objects['SCALE_REFERENCE_TRANSLATION_ONLY'];root.location=(.55,-3,.006743584759533405-.035)
for c in root.users_collection:c.hide_render=False;c.hide_viewport=False
# Direct child objects of old label collection were hidden, but baseline is in its own collection.
def label(text,pos,size=.25):
 cu=bpy.data.curves.new(text,'FONT');cu.body=text;cu.size=size;cu.materials.append(bpy.data.materials['TEST_ONLY_Label']);o=bpy.data.objects.new(text,cu);context.objects.link(o);o.location=pos
# Tall slender ruler uses existing soil cube geometry copied solely for presentation.
rul=base['KIT_Terrain_Patch_1m'].copy();rul.data=rul.data.copy();rul.name='REVIEW_Target165cm_Ruler';context.objects.link(rul)
for v in rul.data.vertices:v.co=(.04*v.co.x,.04*v.co.y,(v.co.z+.55)/.2*1.65)
rul.data.materials.clear();rul.data.materials.append(bpy.data.materials['TEST_ONLY_Human']);rul.location=(-.5,-3,-.035);rul.hide_render=False;rul.hide_set(False);rul['role']='target_design_165cm_ruler'
label('TARGET 1.65m  /  LEGACY 1.8028m',(-2.3,-3.5,.05),.17)
label('AETHERLAB  /  TRANSITIONS v3',(-3,-10,.1),.55);label('ONE CONNECTED ASSEMBLY  |  NOT WORLD LAYOUT',(-3,-9.2,.1),.22)
label('ROCK / SOIL',(-4.5,-6,.1),.24);label('8 STEP / +1m',(-5,1,.1),.24);label('BANK / BRIDGE / BANK',(-5,10,.1),.24);label('RETURN RAMP',(-5,20,.1),.24)
for i,o in enumerate(new):o.location=(-30-i*7,0,0)
# v2 cameras remain as historical data; v3 views are explicitly named.
def cam(name,pos,target,scale):
 cu=bpy.data.cameras.new(name);o=bpy.data.objects.new(name,cu);context.objects.link(o);o.location=pos;o.rotation_euler=(Vector(target)-o.location).to_track_quat('-Z','Y').to_euler();cu.type='ORTHO';cu.ortho_scale=scale;return o
s.camera=cam('CAM_v3_Overview',(27,-32,33),(0,7,.5),42)
cam('CAM_v3_Stair',(10,-7,8),(0,1,.4),12);cam('CAM_v3_Bridge',(10,5,7),(0,11,.4),12);cam('CAM_v3_Rock',(9,-11,4),(2,-6,-.1),8);cam('CAM_v3_Side',(15,10,4),(0,10,.4),20)
s.render.engine='CYCLES';s.cycles.samples=24;s.cycles.use_denoising=False;s.render.resolution_x=1250;s.render.resolution_y=1400;s.render.resolution_percentage=100;s.view_settings.view_transform='AgX'
s['asset_scope']='v3 minimum terrain connections; no world/engine/physics/nav/LOD/performance acceptance';s['scale_contract']='New protagonist target 1.65m ruler only; old unchanged baseline1.8027965m; 4m local candidate'
s['intentional_contacts']='Adjacent planks touch; bridge bearers embed0.25m into banks; posts embed0.14m; rock bottoms embed in soil; stair flank soil partially intersects solid stone'
bpy.context.view_layer.update()
for o in new:
 vs=[v.co for v in o.data.vertices];rows.append({'asset_id':o.name,'role':o['role'],'source':o['derived_from'],'local_sockets_m':json.loads(o['local_sockets_json']),'bounds_m':[[min(v[k] for v in vs) for k in range(3)],[max(v[k] for v in vs) for k in range(3)]],'materials':[m.name for m in o.data.materials],'vertices':len(vs),'faces':len(o.data.polygons)})
report={'version':3,'input_v2_sha256':hashlib.sha256(open(input_path,'rb').read()).hexdigest(),'source_scene_sha256':hashlib.sha256(open(a.source_scene,'rb').read()).hexdigest(),'source_objects':source_record,'new_master_count':len(new),'retained_v2_masters':len(base),'new_fixture_instances':len(yard.objects),'new_masters':rows,'scope':'One connected transition chain; not global world','candidate_pitch_m':4,'stair_candidate':{'rise_m':1,'run_m':4,'risers':8,'riser_m':.125,'tread_m':.5},'bridge_candidate':{'span_m':4,'deck_width_m':4,'abutment_length_m':1,'bearer_embed_m':.25},'limitations':['No destructive bridge/rope/anchor mechanic','No curved/variable-span bridge, all stair dimensions or all rock profiles','Legacy lantern mesh defect remains unused','No UE/nav/collision/structural/load/LOD/performance claims']}
json.dump(report,open(ROOT+'/docs/Transition_Manifest.json','w'),ensure_ascii=False,indent=2)
bpy.ops.wm.save_as_mainfile(filepath=ROOT+'/source/AetherLab_CoreKit_Transitions_v3.blend',compress=True)
print('BUILD_V3',json.dumps({'new_masters':len(new),'instances':len(yard.objects),'saved':bpy.data.filepath}))
