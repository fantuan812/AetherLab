"""Build isolated v2 connection fixtures from the preserved v1 Library blend.
blender -b INPUT_V1.blend --python build_connections.py -- --output-root OUT
No original source, character proportions, runtime asset, or project-wide grid is changed.
"""
import bpy, math, os, sys, json, argparse
from mathutils import Vector
p=argparse.ArgumentParser();p.add_argument('--output-root',required=True);a=p.parse_args(sys.argv[sys.argv.index('--')+1:]);ROOT=os.path.abspath(a.output_root)
for d in ['source','docs','previews']:os.makedirs(os.path.join(ROOT,d),exist_ok=True)
s=bpy.context.scene;s.name='AETHERLAB_CoreKit_Connections_v2'
masters=bpy.data.collections['01_SOURCE_MODULES__4m_Grid'];masters.name='01_MASTER_MODULES__4m_Candidate'
yard=bpy.data.collections['02_LINKED_ASSEMBLY_TEST__Not_World'];yard.name='02_CONNECTION_FIXTURES__NOT_WORLD'
labels=bpy.data.collections['03_LABELS_AND_SCALE'];tech=bpy.data.collections['04_SOCKET_GUIDES']
# Remove only v1 presentation / fixtures. Keep every v1 master and the unchanged character data.
for col in [yard,labels,tech]:
 for o in list(col.objects):bpy.data.objects.remove(o,do_unlink=True)
for o in list(bpy.data.objects):
 if o.type=='CAMERA':bpy.data.objects.remove(o,do_unlink=True)
base={o.name:o for o in masters.objects if o.type=='MESH'}
for o in base.values():o.location=(0,0,0);o.hide_render=True;o.hide_set(True)
new=[];joins=[];fixtures={};sourcefile=bpy.data.filepath
stone=bpy.data.materials['KIT_Wet basalt 1'];earth=bpy.data.materials['KIT_Wet mountain earth'];textmat=bpy.data.materials['TEST_ONLY_Label'];gold=bpy.data.materials['TEST_ONLY_Human'];floor=bpy.data.materials['TEST_ONLY_Charcoal']

def mesh(name,verts,faces,mat,source,role):
 me=bpy.data.meshes.new(name+'_Mesh');me.from_pydata(verts,[],faces);me.update();me.materials.append(mat)
 # All authored faces have outward normals. UV is meter-based and intentionally tiled.
 uv=me.uv_layers.new(name='SurfaceUV')
 for poly in me.polygons:
  for li in poly.loop_indices:
   c=me.vertices[me.loops[li].vertex_index].co;uv.data[li].uv=(c.x,c.y if abs(poly.normal.z)>.3 else c.z)
 o=bpy.data.objects.new(name,me);masters.objects.link(o);o['asset_id']=name;o['derived_from']=source;o['role']=role;o['stage']='v2 isolated connection candidate';o['grid_m']=4.;o.asset_mark();o.hide_render=True;o.hide_set(True);new.append(o);return o

def prism(name,xy,top,bottom,mat=earth,source='new minimum connection geometry',role='terrain_support'):
 n=len(xy);vs=[(x,y,bottom(x,y)) for x,y in xy]+[(x,y,top(x,y)) for x,y in xy]
 fs=[tuple(reversed(range(n))),tuple(range(n,2*n))]+[(i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n)]
 return mesh(name,vs,fs,mat,source,role)

def box(name,x0,x1,y0,y1,z0,z1,mat,role):return prism(name,[(x0,y0),(x1,y0),(x1,y1),(x0,y1)],lambda x,y:z1,lambda x,y:z0,mat,role=role)
def derive(src,name,func,role):
 o=src.copy();o.data=src.data.copy();o.name=name;masters.objects.link(o)
 for v in o.data.vertices:v.co=func(v.co.copy())
 o['asset_id']=name;o['derived_from']=src.name;o['role']=role;o.asset_mark();new.append(o);return o

def inst(master,name,loc=(0,0,0),rot=0,role=None,fixture=None):
 o=master.copy();o.data=master.data;o.name=name;yard.objects.link(o);o.location=loc;o.rotation_euler=(0,0,rot);o.hide_render=False;o.hide_set(False);o['master']=master.name
 if role:o['role']=role
 if fixture:o['fixture']=fixture;fixtures.setdefault(fixture,[]).append(o.name)
 return o

def label(text,pos,size=.35):
 cu=bpy.data.curves.new(text,'FONT');cu.body=text;cu.size=size;cu.extrude=0;cu.materials.append(textmat);o=bpy.data.objects.new(text,cu);labels.objects.link(o);o.location=pos;return o

def fixture_box(name,lo,hi,mat):
 # Presentation only. Never added to geometry validation's support / obstacle set.
 o=box(name,*[v for pair in zip(lo,hi) for v in pair],mat,'presentation')
 masters.objects.unlink(o);labels.objects.link(o);new.remove(o);o.hide_render=False;o.hide_set(False);return o
path=base['KIT_Path_Flagstone_4x4_A'];bed=base['KIT_Path_Substrate_4x4'];ramp=base['KIT_Path_Ramp_4m_Rise1m'];rampbed=base['KIT_Substrate_Ramp_4m_Rise1m'];wall=base['KIT_WallLow_Run_4m'];pier=base['KIT_WallLow_EndCornerPier'];rail=base['KIT_Fence_Rails_4m'];post=base['KIT_Fence_SharedPost']
deep=derive(bed,'KIT_Path_Foundation_4x4',lambda v:Vector((v.x,v.y,-.55 if v.z<-.1 else v.z)),'road_support')
foot=box('KIT_Wall_Footing_4m',-.34,.34,0,4,-.155,.10,stone,'wall_support')
wallrise=derive(wall,'KIT_WallLow_Rise_4m_1m',lambda v:Vector((v.x,v.y,v.z+.25*v.y)),'wall')
footrise=derive(foot,'KIT_Wall_Footing_Rise_4m_1m',lambda v:Vector((v.x,v.y,v.z+.25*v.y)),'wall_support')
railrise=derive(rail,'KIT_Fence_Rails_Rise_4m_1m',lambda v:Vector((v.x,v.y,v.z+.25*v.y)),'fence')
shoulder=prism('KIT_Shoulder_Straight_1m',[(0,0),(1,0),(1,1),(0,1)],lambda x,y:-.035-.315*x,lambda x,y:-.55,role='road_terrain_transition')
# A diagonal edge splits each square: max() gives a convex corner; min() gives a concave corner.
def corner(name,convex):
 vs=[(0,0,-.55),(1,0,-.55),(1,1,-.55),(0,1,-.55)]+[(x,y,-.035-.315*(max(x,y) if convex else min(x,y))) for x,y in [(0,0),(1,0),(1,1),(0,1)]]
 return mesh(name,vs,[(3,2,1,0),(4,5,6),(4,6,7),(0,1,5,4),(1,2,6,5),(2,3,7,6),(3,0,4,7)],earth,'new slope closure, shared earth material','road_terrain_transition')
conv=corner('KIT_Shoulder_Convex90_1m',True);conc=corner('KIT_Shoulder_Concave90_1m',False)
soil=box('KIT_Terrain_Patch_1m',0,1,0,1,-.55,-.35,earth,'terrain')
soilhigh=box('KIT_Terrain_RaisedPatch_1m',0,1,0,1,-.55,.965,earth,'terrain')
# Solid ramp bank has a flat base and two slopes to ground, not a floating thin slab.
def bank(name,rise,plateau=False):
 vs=[];fs=[]
 for j in range(5):
  z=rise if plateau else j*rise/4
  for x,h in [(-3,-.35),(-2,z-.035),(-2,z-.155),(2,z-.155),(2,z-.035),(3,-.35),(3,-.55),(-3,-.55)]:vs.append((x,j,h))
 for j in range(4):
  for k in range(8):
   n=j*8+k;v=j*8+(k+1)%8;fs.extend([(n,v,v+8),(n,v+8,n+8)])
 fs.extend([tuple(reversed(range(8))),tuple(range(32,40))])
 ob=mesh(name,vs,fs,earth,'existing earth material; solid cross-section follows flat ground','ramp_terrain_support')
 import bmesh
 bm=bmesh.new();bm.from_mesh(ob.data);bmesh.ops.recalc_face_normals(bm,faces=bm.faces);bm.to_mesh(ob.data);bm.free()
 return ob
bankr=bank('KIT_Ramp_Bank_4m_Rise1m',1);bankh=bank('KIT_Raised_Landing_Bank_4m_Height1m',1,True)
endbank=derive(bankh,'KIT_Raised_EndBank_1m',lambda v:Vector((v.x,v.y*.25,v.z)),'ramp_terrain_support')
# End cap itself is exposed soil: fill the substrate recess rather than leave a 0.12m trench.
for v in endbank.data.vertices:
 if abs(abs(v.co.x)-2)<1e-6 and v.co.z>.5:v.co.z=.965
import bmesh
bm=bmesh.new();bm.from_mesh(endbank.data);bmesh.ops.remove_doubles(bm,verts=bm.verts,dist=1e-6);bmesh.ops.dissolve_degenerate(bm,edges=bm.edges,dist=1e-6);bm.to_mesh(endbank.data);bm.free()
# Road fixtures: a true closed loop, T branches and a separate X junction; boundary transitions are linked 1m pieces.
def roads(name,cells):
 occupied=set()
 for i,j in cells:
  cx=4*i;cy=4*j
  inst(path,f'{name}_Paving_{i}_{j}',(cx,cy,0),role='road_surface',fixture=name)
  inst(deep,f'{name}_Base_{i}_{j}',(cx,cy,0),role='road_support',fixture=name)
  occupied.update((x,y) for x in range(cx-2,cx+2) for y in range(cy,cy+4))
 # Each unit cell outside the footprint resolves its adjacency by actual topology, not sculpting individual patches.
 nearby={(x+dx,y+dy) for x,y in occupied for dx in [-1,0,1] for dy in [-1,0,1]}-occupied
 for x,y in sorted(nearby):
  # Rotation maps master local unit square to this cell; master road at x=0 (straight) or origin (corner).
  side=[(x-1,y) in occupied,(x,y-1) in occupied,(x+1,y) in occupied,(x,y+1) in occupied]
  if sum(side)==1:
   idx=side.index(True);r=[0,math.pi/2,math.pi,3*math.pi/2][idx];pos=[(x,y,0),(x+1,y,0),(x+1,y+1,0),(x,y+1,0)][idx];m=shoulder
  elif sum(side)==2:
   # Roads touch x=0 and y=0 for canonical concave piece.
   pairs=[(0,1),(1,2),(2,3),(3,0)];idx=next(k for k,(u,v) in enumerate(pairs) if side[u] and side[v]);r=idx*math.pi/2;pos=[(x,y,0),(x+1,y,0),(x+1,y+1,0),(x,y+1,0)][idx];m=conc
  elif sum(side)==0:
   diag=[(x-1,y-1) in occupied,(x+1,y-1) in occupied,(x+1,y+1) in occupied,(x-1,y+1) in occupied]
   if not any(diag):continue
   idx=diag.index(True);r=idx*math.pi/2;pos=[(x,y,0),(x+1,y,0),(x+1,y+1,0),(x,y+1,0)][idx];m=conv
  else:raise RuntimeError('unsupported boundary topology')
  inst(m,f'{name}_Transition_{x}_{y}',pos,r,fixture=name)
 return occupied
loopcells=[(i,j) for i in range(4) for j in range(4) if i in [0,3] or j in [0,3]]+[(4,1)]
roads('ROAD_LOOP_T',loopcells)
roads('ROAD_X',[(7,1),(6,1),(8,1),(7,0),(7,2)])
# Explicit road-to-earth terminus extends the south arm of the cross beyond the shared transition.
for x in range(26,30):
 for y in [-3,-2]:inst(soil,f'END_Earth_{x}_{y}',(x,y,0),fixture='ROAD_EARTH_END')
# Low-wall and fence closed loops. One unique shared support per junction, plus fence midspan supports.
def barrier_segment(m,fm,kind,name,p0,p1,fix):
 d=Vector(p1)-Vector(p0);rot=math.atan2(-d.x,d.y);o=inst(m,name,p0,rot,role=kind,fixture=fix)
 o['joint_start']=list(p0);o['joint_end']=list(p1)
 if fm:inst(fm,name+'_Foot',p0,rot,fixture=fix)
 joins.append(dict(object=o.name,kind=kind,start=list(p0),end=list(p1)))
 return o

def barrier_loop(fix,origin,kind):
 x,y=origin;points=[(x,y,0),(x+4,y,0),(x+8,y,0),(x+8,y+4,0),(x+8,y+8,0),(x+4,y+8,0),(x,y+8,0),(x,y+4,0)]
 for k,p0 in enumerate(points):
  p1=points[(k+1)%len(points)];barrier_segment(wall if kind=='wall' else rail,foot if kind=='wall' else None,kind,fix+f'_Run{k}',p0,p1,fix)
  inst(pier if kind=='wall' else post,fix+f'_Joint{k}',(p0[0],p0[1],-.14),role='joint_support',fixture=fix)
  if kind=='fence':inst(post,fix+f'_Mid{k}',((p0[0]+p1[0])/2,(p0[1]+p1[1])/2,-.14),role='joint_support',fixture=fix)
 # Continuous ground under this fixture, explicitly geometry in its own support collection, not presentation floor.
 for i in range(x-1,x+10):
  for j in range(y-1,y+10):inst(soil,fix+f'_Ground_{i}_{j}',(i,j,.315),role='barrier_ground',fixture=fix)
barrier_loop('WALL_LOOP',(-1,-14),'wall');barrier_loop('FENCE_LOOP',(10,-14),'fence')
# Mixed wall/fence junction network: existing pier is the 4-way coupler and end cap. No redundant fence post in the pier.
for k,(dx,dy,kind) in enumerate([(0,4,'wall'),(4,0,'fence'),(0,-4,'fence'),(-4,0,'wall')]):
 p0=(4+dx,21+dy,0);p1=(4,21,0)
 barrier_segment(wall if kind=='wall' else rail,foot if kind=='wall' else None,kind,f'MIX_X_Run{k}',p0,p1,'MIXED_X_ENDS')
 inst(pier if kind=='wall' else post,f'MIX_X_End{k}',(p0[0],p0[1],-.14),role='joint_support',fixture='MIXED_X_ENDS')
 if kind=='fence':inst(post,f'MIX_X_Mid{k}',((p0[0]+p1[0])/2,(p0[1]+p1[1])/2,-.14),role='joint_support',fixture='MIXED_X_ENDS')
inst(pier,'MIX_X_OneSharedPier',(4,21,-.14),role='joint_support',fixture='MIXED_X_ENDS')
for x in range(-1,10):
 for y in range(16,27):inst(soil,f'MIX_Ground{x}_{y}',(x,y,.315),role='barrier_ground',fixture='MIXED_X_ENDS')
# Height transition fixture: flat -> +1m over4m -> high landing, with wall on left / fence right.
for suffix,m,sm,pos in [('LOW',path,deep,(26,-14,0)),('RISE',ramp,bankr,(26,-10,0)),('HIGH',path,bankh,(26,-6,1))]:
 inst(m,'HEIGHT_'+suffix+'_Paving',pos,role='road_surface',fixture='HEIGHT_MIX')
 if suffix=='HIGH':inst(sm,'HEIGHT_HIGH_Bank',(26,-6,0),fixture='HEIGHT_MIX')
 else:inst(sm,'HEIGHT_'+suffix+'_Bank',pos,fixture='HEIGHT_MIX')
 if suffix=='RISE':inst(rampbed,'HEIGHT_RISE_Bed',pos,role='road_support',fixture='HEIGHT_MIX')
 # Use the original thin landing substrate because its solid bank is already present.
 if suffix=='HIGH':inst(bed,'HEIGHT_HIGH_Bed',pos,role='road_support',fixture='HEIGHT_MIX')
# Low-level side shoulders meet the full bank cross-section at the ramp start.
for y in range(-14,-10):
 inst(shoulder,f'HEIGHT_LowShoulderR{y}',(28,y,0),fixture='HEIGHT_MIX');inst(shoulder,f'HEIGHT_LowShoulderL{y}',(24,y+1,0),math.pi,fixture='HEIGHT_MIX')
for side,kind in [(-1,'wall'),(1,'fence')]:
 x=24.6 if side==-1 else 27.8
 for k,(y,z) in enumerate([(-14,0),(-10,0),(-6,1)]):
  rising=k==1;m=(wallrise if rising else wall) if kind=='wall' else (railrise if rising else rail);f=(footrise if rising else foot) if kind=='wall' else None
  barrier_segment(m,f,kind,f'HEIGHT_{kind}_{k}',(x,y,z),(x,y+4,z+(1 if rising else 0)),'HEIGHT_MIX')
  # Endpoint piers remain upright and are embedded 0.14m into the bank. No skeleton or part scaling.
  inst(pier if kind=='wall' else post,f'HEIGHT_{kind}_Node{k}',(x,y,z-(.21 if kind=='wall' and k==0 else .18 if kind=='wall' else .14)),role='joint_support',fixture='HEIGHT_MIX')
  if kind=='fence':inst(post,f'HEIGHT_Fence_Mid{k}',(x,y+2,z+(.5 if rising else 0)-.14),role='joint_support',fixture='HEIGHT_MIX')
 inst(pier if kind=='wall' else post,f'HEIGHT_{kind}_Node3',(x,-2,.82 if kind=='wall' else .86),role='joint_support',fixture='HEIGHT_MIX')
# At top, a perpendicular fence joins the low-wall pier; a 2.58m nominal opening is left down the road.
barrier_segment(rail,None,'fence','HEIGHT_CrossKit_Rail',(20.6,-2,1),(24.6,-2,1),'HEIGHT_MIX')
inst(post,'HEIGHT_CrossKit_End',(20.6,-2,.86),role='joint_support',fixture='HEIGHT_MIX')
inst(post,'HEIGHT_CrossKit_Mid',(22.6,-2,.86),role='joint_support',fixture='HEIGHT_MIX')
# Small raised terrain support below the perpendicular cross-kit connection.
for x in range(20,25):
 for y in [-3,-2]:inst(soilhigh,f'HEIGHT_CrossSoil{x}_{y}',(x,y,0),role='barrier_ground',fixture='HEIGHT_MIX')
# Reusable upper-bank cap gives both terminal supports a full real foundation.
inst(endbank,'HEIGHT_UpperEndCap',(26,-2,0),fixture='HEIGHT_MIX')
# Low road terminus blends to earth; stone pier is embedded0.21m to support its downslope corners.
for x in range(24,28):inst(shoulder,f'HEIGHT_LowerEnd{x}',(x,-14,0),-math.pi/2,fixture='HEIGHT_MIX')
# Keep independently reviewed fixture groups spatially separate, including their terrain support.
for o in yard.objects:
 if o.get('fixture')=='HEIGHT_MIX':o.location.y-=2
 if o.get('fixture')=='MIXED_X_ENDS':o.location.y+=3
# Present unscaled approved character and 1.65m ruler on the low ramp approach.
root=bpy.data.objects['SCALE_REFERENCE_TRANSLATION_ONLY'];root.location=(26,-14,.006743584759533405-.035)
root['note']='Translation and review material only; original 65-bone source unchanged'
fixture_box('DESIGN_1p65m_Ruler',(26.85,-14.1,-.035),(26.89,-14.06,1.615),gold)
for k in range(12):
 z=-.035+k*.15
 if z<=1.615:fixture_box(f'RULER_Tick{k}',(26.78,-14.12,z),(26.94,-14.04,z+.009),textmat)
label('1.65m DESIGN / 1.8028m SOURCE',(24.4,-16.7,.05),.17)
# Place all master modules in a compact hidden library; full per-module geometry measured separately.
for i,o in enumerate(masters.objects):
 if o.type=='MESH':o.location=(-14-(i%4)*6,(i//4)*6,0);o.hide_render=True;o.hide_set(True)
# Test platform is intentionally 0.8m below route grade, so edge support is visible in side view.
fixture_box('TEST_ONLY_Platform',(-4,-18,-.95),(38,33,-.80),floor)
label('AETHERLAB  /  SHARED CORE v2',(-2,-17,.02),.7)
label('CONNECTION FIXTURES  |  NOT WORLD LAYOUT  |  4m CANDIDATE',(-2,-16,.02),.28)
label('A  ROAD LOOP + T',(-1,16,.05),.38);label('B  X + EARTH END',(20,13,.05),.38)
label('C  WALL / CLOSED',(-1,-4.8,.05),.32);label('D  FENCE / CLOSED',(10,-4.8,.05),.32)
label('E  +1m RAMP / MIXED',(22,-2.8,.1),.3);label('F  WALL-FENCE X / END CAPS',(-1,30,.05),.3)
s['asset_scope']='SharedCore v2: isolated reusable road, wall, fence connection fixtures only; not whole-kit/world/engine acceptance'
s['source_v1']=os.path.basename(sourcefile);s['scale_contract']='4m pitch candidate only; design1.65m vs actual1.8027965m unchanged';s['intentional_intersections']='Stone in substrate; wall runs/rails into single piers; posts buried0.14m; adjacent closed soil cells share coplanar internal faces'
s['geometry_validation']='awaiting independent reopen and per-fixture mesh measurement'
# Use the unchanged source lighting; cameras are saved and render only final-file fixtures.
def camera(name,loc,target,scale):
 c=bpy.data.cameras.new(name);o=bpy.data.objects.new(name,c);s.collection.objects.link(o);o.location=loc;o.rotation_euler=(Vector(target)-o.location).to_track_quat('-Z','Y').to_euler();c.type='ORTHO';c.ortho_scale=scale;return o
s.camera=camera('CAM_Overview',(48,-52,55),(15,6,0),61)
camera('CAM_Connections_Close',(43,-32,25),(24,-10,.3),22)
camera('CAM_Road_Top',(9,8,50),(9,8,0),24)
camera('CAM_Junction_Close',(14,10,11),(4,24,0),15)
camera('CAM_Height_Side',(42,-8,5),(26,-10,.3),18)
s.render.engine='CYCLES';s.cycles.samples=28;s.cycles.use_denoising=False;s.render.resolution_x=1500;s.render.resolution_y=1200;s.render.resolution_percentage=100;s.render.image_settings.file_format='PNG';s.view_settings.view_transform='AgX'
for screen in bpy.data.screens:
 for area in screen.areas:
  if area.type=='VIEW_3D':area.spaces.active.region_3d.view_perspective='CAMERA';area.spaces.active.clip_end=1000
readme=bpy.data.texts.get('README_CoreKit') or bpy.data.texts.new('README_CoreKit');readme.clear();readme.write('SharedCore v2 isolated connection test. Original v1 and source assets remain separate and unchanged.\n22 linked master meshes, ground transition pieces reusable without per-instance stretching.\n4m remains a candidate pitch, not a global standard.\nRead docs/Connections_Validation.json for measured scope and remaining limits.\nNo UE/navigation/performance or full world acceptance.\n')
# Correct v2 production copies only: inward legacy substrate normals and zero-area side UV projections.
import bmesh
for ob in list(new)+[bed,rampbed]:
 bm=bmesh.new();bm.from_mesh(ob.data);bmesh.ops.recalc_face_normals(bm,faces=bm.faces);bm.to_mesh(ob.data);bm.free();ob.data.update()
 uv=ob.data.uv_layers.get('SurfaceUV') or ob.data.uv_layers.new(name='SurfaceUV')
 for poly in ob.data.polygons:
  axis=max(range(3),key=lambda k:abs(poly.normal[k]));axes=[k for k in range(3) if k!=axis]
  for li in poly.loop_indices:
   v=ob.data.vertices[ob.data.loops[li].vertex_index].co;uv.data[li].uv=(v[axes[0]],v[axes[1]])
 ob['v2_cleanup']='Outward normals and per-face dominant-axis SurfaceUV; original v1 file unchanged'
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from connection_metadata import refresh
refresh(ROOT)
bpy.ops.wm.save_as_mainfile(filepath=ROOT+'/source/AetherLab_CoreKit_Interface_v2.blend',compress=True)
manifest={'version':2,'source_v1_library':'libfile_3dc1fbb1e36881919c4dce2f9c37a35e','source_v1_sha256':'451da979d79475cbed1ae9974dfe7193458d6d6899f9269434a125480fc18b25','validation_status':'awaiting_reopen','fixtures':fixtures,'barrier_segments':joins,'declared_coordinate_space':'fixture layout before final group translations; not measured world coordinates','fixture_translation_m':{'HEIGHT_MIX':[0,-2,0],'MIXED_X_ENDS':[0,3,0]},'original_master_count':len(base),'new_master_count':len(new),'new_masters':[{'asset_id':o.name,'source':o.get('derived_from'),'role':o.get('role')} for o in new],'candidate_pitch_m':4,'design_height_m':1.65,'source_character_height_m':1.8027965174987912,'limitations':['Only core road/wall/fence connections; not all scene kits','No stairs/bridge-bank/rock-wall production kits; inherited unused lantern contains 112 degenerate quads (224 triangles)','No engine, navigation, collision, LOD or performance acceptance','Paving stone spacing is intentional; solid soil substrate closes visual holes','Upright posts usually buried0.14m; height-fixture first wall pier0.21m and other wall piers0.18m; banks visibly slope','Contact tests must use actual mesh geometry, not this declaration']}
json.dump(manifest,open(ROOT+'/docs/Connections_Manifest.json','w'),ensure_ascii=False,indent=2)
print(json.dumps({'saved':bpy.data.filepath,'master_count':len(base)+len(new),'fixtures':{k:len(v) for k,v in fixtures.items()}}))
