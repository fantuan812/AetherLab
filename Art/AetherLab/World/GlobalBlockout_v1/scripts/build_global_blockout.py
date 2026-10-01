"""AetherLab six-region connected blockout. Blender 4.3+, metres.
New geographic terrain / circulation / identifiable specialist proxies only.
All input files are read-only. Outputs a distinct world identity, not a kit replacement.
"""
import bpy, math, json, os, sys, argparse, hashlib, random
from mathutils import Vector, Matrix
P=argparse.ArgumentParser();P.add_argument('--kit',required=True);P.add_argument('--legacy',required=True);P.add_argument('--output',required=True)
a=P.parse_args(sys.argv[sys.argv.index('--')+1:]);ROOT=os.path.abspath(a.output)
for d in ['source','docs','previews','stages']:os.makedirs(ROOT+'/'+d,exist_ok=True)
random.seed(84012)
bpy.ops.wm.read_factory_settings(use_empty=True)
s=bpy.context.scene;s.name='AETHERLAB_WORLD_800m_BLOCKOUT_v1';s.unit_settings.system='METRIC';s.unit_settings.scale_length=1
s.world=bpy.data.worlds.new('Overcast blue grey');s.world.use_nodes=True;s.world.node_tree.nodes['Background'].inputs[0].default_value=(.29,.37,.42,1);s.world.node_tree.nodes['Background'].inputs[1].default_value=.7

def coll(n,parent=None):
 c=bpy.data.collections.new(n);(parent or s.collection).children.link(c);return c
C={k:coll(k) for k in ['00_CONTINUOUS_TERRAIN','01_MAIN_ROUTES','02_TOWN_SCN02_07','03_MOUNTAIN_SCN01','04_FOREST_SCN08','05_WATERWORKS_SCN09','06_ABBEY_SCN10_11','07_RELAY_SCN12','08_CONTEXT_FOLIAGE','09_REVIEW_CAMERAS','10_ANNOTATIONS__NOT_GAMEPLAY','11_GEOMETRY_GUIDES__NOT_GAMEPLAY']}
M=coll('90_REUSE_MASTERS__HIDDEN');M.hide_render=True;M.hide_viewport=True
D=coll('91_LEGACY_SOURCE_COMPONENTS__HIDDEN');D.hide_render=True;D.hide_viewport=True
G=C['11_GEOMETRY_GUIDES__NOT_GAMEPLAY'];G.hide_render=True
ANN=C['10_ANNOTATIONS__NOT_GAMEPLAY'];ANN.hide_render=True
master={};source_records=[];routes=[];scenes=[];proxies=[];instances=[]
# Import original kit masters only, never any of its 755 fixture placements.
with bpy.data.libraries.load(os.path.abspath(a.kit),link=False) as (fr,to):
 to.collections=['01_MASTER_MODULES__4m_Candidate','05_TRANSITION_MASTERS__CANDIDATES','05_EXISTING_CHARACTER__UNSCALED_65_BONES']
for c in to.collections:
 if 'CHARACTER' in c.name:
  C['09_REVIEW_CAMERAS'].children.link(c);c.hide_render=False;c.hide_viewport=False
  character_collection=c
 else:
  M.children.link(c)
  for o in c.objects:
   if o.type=='MESH':master[o.name]=o;o['origin_file']='SharedCore_v3';o['reuse_class']='unchanged_kit_master'
# Read selected source collections. Keep source pieces and produce shared combined presentation masters.
source_collections=['SCN01_Shelter_Refinement','SM_BrokenCart','SM_RescuePlatform','SM_WaterBarrel','FX_ShelteredFire','SCN01_House01_Refinement','SCN01_Two_Visible_Roof_Variants','REFINED_Valley']
with bpy.data.libraries.load(os.path.abspath(a.legacy),link=False) as (fr,to):to.collections=source_collections
SRC={c.name:c for c in to.collections}
for c in to.collections:D.children.link(c)
# For legacy evaluation the source is briefly linked, but never moved or changed geometrically.
D.hide_viewport=False
for c in to.collections:c.hide_viewport=False
bpy.context.view_layer.update()

def merge_master(name,obs,anchor=None):
 obs=[o for o in obs if o.type=='MESH'];vs=[];fs=[];mi=[];mats=[];uvs=[]
 dep=bpy.context.evaluated_depsgraph_get()
 if anchor is None:
  bb=[o.matrix_world@Vector(v) for o in obs for v in o.bound_box];lo=Vector([min(v[k] for v in bb) for k in range(3)]);hi=Vector([max(v[k] for v in bb) for k in range(3)]);anchor=Vector(((lo.x+hi.x)/2,(lo.y+hi.y)/2,lo.z))
 anchor=Vector(anchor)
 for o in obs:
  ev=o.evaluated_get(dep);me=ev.to_mesh(preserve_all_data_layers=True,depsgraph=dep);off=len(vs);vs += [tuple(o.matrix_world@v.co-anchor) for v in me.vertices];remap={}
  for j,mat in enumerate(me.materials):
   mat=mat.original if mat and mat.is_evaluated else mat
   if mat not in mats:mats.append(mat)
   remap[j]=mats.index(mat)
  ul=me.uv_layers.active
  for p in me.polygons:
   fs.append(tuple(off+i for i in p.vertices));mi.append(remap.get(p.material_index,0));uvs += [tuple(ul.data[i].uv) if ul else (0,0) for i in p.loop_indices]
  ev.to_mesh_clear()
 me=bpy.data.meshes.new(name+'_SharedMesh');me.from_pydata(vs,[],fs);me.update()
 for mat in mats:
  if mat:me.materials.append(mat)
 for p,idx in zip(me.polygons,mi):p.material_index=idx
 ul=me.uv_layers.new(name='SurfaceUV')
 for i,uv in enumerate(uvs):ul.data[i].uv=uv
 if any(mat and mat.use_nodes and any(nd.type=='UVMAP' and nd.uv_map=='AtlasUV' for nd in mat.node_tree.nodes) for mat in mats):
  au=me.uv_layers.new(name='AtlasUV')
  for i,uv in enumerate(uvs):au.data[i].uv=uv
 o=bpy.data.objects.new(name,me);M.objects.link(o);o['reuse_class']='legacy_geometry_normalized_translation';o['source_objects_json']=json.dumps([x.name for x in obs]);o['source_anchor_m']=list(anchor);o['origin_file']='SCN01_Shelter_TA_Candidate';master[name]=o
 source_records.append({'master':name,'source_objects':[x.name for x in obs],'source_anchor_m':list(anchor),'object_count':len(obs),'vertices':len(vs),'faces':len(fs),'change':'evaluated geometry joined for linked reuse; translated origin only; materials retained'})
 return o
merge_master('SRC_Shelter_Exact',SRC['SCN01_Shelter_Refinement'].objects,(-4.7,0,-.035))
merge_master('SRC_AccidentCart_Exact',SRC['SM_BrokenCart'].objects)
merge_master('SRC_RescuePlatform_Exact',SRC['SM_RescuePlatform'].objects)
merge_master('SRC_Barrel_Exact',SRC['SM_WaterBarrel'].objects)
merge_master('SRC_ShelteredFire_Exact',SRC['FX_ShelteredFire'].objects)
merge_master('SRC_TownHouse_Exact',SRC['SCN01_House01_Refinement'].objects)
for typ in ['H02','H03']:
 merge_master('SRC_Roof_'+typ,[o for o in SRC['SCN01_Two_Visible_Roof_Variants'].objects if o.name.startswith(typ) and any(x in o.name for x in ['Roof','slate','ridge','eave'])])
valley=list(SRC['REFINED_Valley'].objects)
for i in range(2):
 names=['Natural tree trunk'+('' if i==0 else '.001')]+[p+('' if j==0 else f'.{j:03d}') for j in range(i*16,(i+1)*16) for p in ['Branching tree limb','Fine tree crown']]
 merge_master('SRC_Tree_'+str(i),[o for o in valley if o.name in names])
for src,name in [('Wild shrub leaves','SRC_Shrub'),('Fern and broadleaf undergrowth','SRC_Fern'),('Tall wet grass','SRC_Grass'),('River rock','SRC_Rock'),('Natural tree trunk','SRC_Trunk')]:merge_master(name,[o for o in valley if o.name==src])
# Keep the exact source shelter pieces in the file. Other imported pieces no longer needed are removed only from this new file.
for cname,c in SRC.items():
 if cname=='SCN01_Shelter_Refinement':c.name='ARCHIVE_Original_SCN01_Shelter_Components';continue
 for o in list(c.objects):bpy.data.objects.remove(o,do_unlink=True)
 bpy.data.collections.remove(c)
D.hide_viewport=True

def mat(n,c,metal=0,rough=.82,alpha=1):
 m=bpy.data.materials.new(n);m.diffuse_color=(*c,alpha);m.use_nodes=True;p=m.node_tree.nodes.get('Principled BSDF');p.inputs['Base Color'].default_value=(*c,alpha);p.inputs['Roughness'].default_value=rough;p.inputs['Metallic'].default_value=metal;p.inputs['Alpha'].default_value=alpha;return m
stone=bpy.data.materials.get('KIT_Wet basalt 1');wood=bpy.data.materials.get('KIT_Aged cedar');earth=mat('GB_Terrain_Cool_Moss',(.155,.21,.19));roadmat=mat('GB_Continuous_Route_Substrate',(.29,.34,.33));plaster=bpy.data.materials.get('H01_Lime_plaster') or mat('GB_Pale_Plaster',(.57,.60,.54));slate=mat('GB_Roof_Slate',(.12,.20,.23));water=mat('GB_Water_Separate_Proxy',(.10,.33,.40),.15,.26);copper=mat('GB_Specialist_Proxy_Copper',(.46,.28,.14),.4);amber=mat('GB_Warm_Landmark',(.85,.49,.13),.2);p=amber.node_tree.nodes.get('Principled BSDF');p.inputs['Emission Color'].default_value=(1,.4,.06,1);p.inputs['Emission Strength'].default_value=.7
proxy=mat('GB_Placeholder_Teal',(.18,.44,.43));glass=mat('GB_Greenhouse_Glass_Proxy',(.42,.60,.58),.05,.35,.35);dark=mat('GB_Hazard_Charcoal',(.10,.115,.105));mark=mat('GB_Review_Annotation',(.84,.89,.83));manmat=mat('GB_Scale165cm',(.80,.52,.21));dry=mat('GB_Dry_Stone_Safe_Proxy',(.53,.56,.49))

def mesh(n,vs,fs,ma,col,role=None):
 me=bpy.data.meshes.new(n);me.from_pydata(vs,[],fs);me.update()
 uv=me.uv_layers.new(name='SurfaceUV')
 for face in me.polygons:
  ax=max(range(3),key=lambda k:abs(face.normal[k]));axes=[k for k in range(3) if k!=ax]
  for li in face.loop_indices:
   v=me.vertices[me.loops[li].vertex_index].co;uv.data[li].uv=(v[axes[0]],v[axes[1]])
 if ma:me.materials.append(ma)
 o=bpy.data.objects.new(n,me);col.objects.link(o)
 if role:o['role']=role
 return o

def box(n,loc,d,ma,col,role='specialist_blockout',scn=None):
 x,y,z=[v/2 for v in d];v=[(-x,-y,-z),(-x,-y,z),(-x,y,-z),(-x,y,z),(x,-y,-z),(x,-y,z),(x,y,-z),(x,y,z)];f=[(0,2,6,4),(1,5,7,3),(0,4,5,1),(2,3,7,6),(0,1,3,2),(4,6,7,5)];o=mesh(n,v,f,ma,col,role);o.location=loc
 if role=='specialist_blockout':o['production_status']='explicit_placeholder';proxies.append(n)
 if scn:o['scene_id']=scn
 return o

def cyl(n,loc,r,depth,ma,col,vertices=16,role='specialist_blockout'):
 vs=[(r*math.cos(i*2*math.pi/vertices),r*math.sin(i*2*math.pi/vertices),z) for z in [-depth/2,depth/2] for i in range(vertices)];f=[tuple(reversed(range(vertices))),tuple(range(vertices,2*vertices))]+[(i,(i+1)%vertices,(i+1)%vertices+vertices,i+vertices) for i in range(vertices)];o=mesh(n,vs,f,ma,col,role);o.location=loc
 if role=='specialist_blockout':o['production_status']='explicit_placeholder';proxies.append(n)
 return o

def inst(key,n,pos,rot=0,col=None,role='reused_instance'):
 m=master[key];o=bpy.data.objects.new(n,m.data);(col or C['08_CONTEXT_FOLIAGE']).objects.link(o);o.location=pos;o.rotation_euler.z=rot;o['master']=key;o['role']=role;o['reuse_class']=m.get('reuse_class','unchanged_kit_master');instances.append({'object':n,'master':key});return o

def line(n,pts,r,ma,col,role='specialist_blockout'):
 cu=bpy.data.curves.new(n,'CURVE');cu.dimensions='3D';cu.resolution_u=1;cu.bevel_depth=r;cu.bevel_resolution=0;sp=cu.splines.new('POLY');sp.points.add(len(pts)-1)
 for p,v in zip(sp.points,pts):p.co=(*v,1)
 cu.materials.append(ma);o=bpy.data.objects.new(n,cu);col.objects.link(o);o['role']=role;return o

def anchor(n,loc,scn=None):
 o=bpy.data.objects.new(n,None);G.objects.link(o);o.location=loc;o.empty_display_type='ARROWS';o.empty_display_size=1
 if scn:o['scene_id']=scn
 return o

def humanoid(n,p,col):
 x,y,z=p;cyl(n+'_Body',(x,y,z+.77),.22,1.08,manmat,col,12,'scale_reference');cyl(n+'_Head',(x,y,z+1.5),.15,.30,manmat,col,12,'scale_reference')
 for xx in [-.12,.12]:box(n+'_Leg'+str(xx),(x+xx,y,z+.25),(.15,.19,.5),manmat,col,'scale_reference')
 anchor(n+'_165cm_DATUM',p)

def roof(n,x,y,z,w,d,h,col,ma=slate):
 vs=[(-w/2,-d/2,0),(w/2,-d/2,0),(w/2,d/2,0),(-w/2,d/2,0),(0,-d/2,h),(0,d/2,h)];o=mesh(n,vs,[(0,4,5,3),(4,1,2,5),(0,1,4),(3,5,2),(0,3,2,1)],ma,col,'architectural_blockout');o.location=(x,y,z);o['production_status']='explicit_placeholder';return o

def floor(n,cx,cy,z,w,d,col,ma=stone):return box(n,(cx,cy,z-.25),(w,d,.5),ma,col,'walkable_floor')

def room(n,x,y,z,w,d,h,col,door='east',roof_kind='gable'):
 floor(n+'_ClearActivityFloor',x,y,z,w,d,col)
 # Wall volumes lie outside activity rectangle. Door leaves 4m clear.
 for side in ['east','west']:
  sx=x+(w/2+.25)*(1 if side=='east' else -1)
  if side==door:
   for sy in [-1,1]:box(n+'_'+side+'_Pier'+str(sy),(sx,y+sy*(d/4+1),z+h/2),(.5,d/2-2,h),plaster,col,'architecture_wall')
   box(n+'_DoorLintel',(sx,y,z+3.2+(h-3.2)/2),(.5,4,h-3.2),wood,col,'architecture_wall')
  else:box(n+'_'+side,(sx,y,z+h/2),(.5,d+1,h),plaster,col,'architecture_wall')
 for sy in [-1,1]:box(n+'_Wall'+str(sy),(x,y+sy*(d/2+.25),z+h/2),(w,.5,h),plaster,col,'architecture_wall')
 for xx in [-w/2,w/2]:
  for yy in [-d/2,d/2]:box(n+'_TimberPost'+str((xx,yy)),(x+xx+(.16 if xx>0 else -.16),y+yy+(.16 if yy>0 else -.16),z+h/2),(.32,.32,h),wood,col,'architectural_blockout')
 if roof_kind=='gable':roof(n+'_Roof',x,y,z+h,w+1.8,d+1.8,2,col)
 if roof_kind=='glass':
  roof(n+'_GreenhouseRoof',x,y,z+h,w+1,d+1,1.7,col,glass)
  for xx in [-w/2,0,w/2]:line(n+'_GlazingFrame'+str(xx),[(x+xx,y-d/2,z+h),(x+xx,y,z+h+1.7),(x+xx,y+d/2,z+h)],.10,copper,col)
 return anchor(n+'_ActivityBounds',(x,y,z))

# Geographic routes: authored continuous ribbons for non-grid geographic alignment.
# Individual route widths and elevation choices are blockout choices, not new global standards.
def route(n,pts,width=6,kind='main',skip=None):
 pts=[tuple(v) for v in pts];routes.append({'id':n,'points_m':pts,'width_m':width,'kind':kind,'skipped_segments':skip or []});vs=[];fs=[]
 # One mitered edge at each polyline vertex; adjacent segments share exact corners.
 sides=[]
 for i,p in enumerate(pts):
  tangents=[]
  for aa,bb in ([(pts[i-1],p)] if i else [])+([(p,pts[i+1])] if i+1<len(pts) else []):
   v=Vector((bb[0]-aa[0],bb[1]-aa[1],0)).normalized();tangents.append(Vector((-v.y,v.x,0)))
  side=sum(tangents,Vector((0,0,0))).normalized();den=max(.5,side.dot(tangents[0]));sides.append(side*width/2/den)
 for j,(p,q) in enumerate(zip(pts,pts[1:])):
  if skip and j in skip:continue
  p=Vector(p);q=Vector(q);dif=q-p;steps=max(1,int(Vector((dif.x,dif.y)).length/2));side=Vector((-dif.y,dif.x,0)).normalized()*width/2
  for k in range(steps):
   A=p+dif*k/steps;B=p+dif*(k+1)/steps;sa=sides[j].lerp(sides[j+1],k/steps);sb=sides[j].lerp(sides[j+1],(k+1)/steps);off=len(vs);vs += [tuple(A-sa),tuple(A+sa),tuple(B+sb),tuple(B-sb)];fs.append((off,off+3,off+2,off+1))
 o=mesh('ROUTE_'+n,vs,fs,roadmat,C['01_MAIN_ROUTES'],'walkable_route');o['route_id']=n;o['authored_choice_width_m']=width
 # Slightly embedded solid side walls for readable roadbed, separate from surface mesh.
 for j,(p,q) in enumerate(zip(pts,pts[1:])):
  if skip and j in skip:continue
  p=Vector(p);q=Vector(q);v=q-p;side=Vector((-v.y,v.x,0)).normalized()*width/2
  for sign in [-1,1]:
   aa=p+sides[j]*sign;bb=q+sides[j+1]*sign;mesh(n+f'_BED_{j}_{sign}',[tuple(aa),tuple(bb),tuple(bb-Vector((0,0,.35))),tuple(aa-Vector((0,0,.35)))],[(0,1,2,3)],earth,C['01_MAIN_ROUTES'],'route_bed')
 return o
# Region entry and exit anchor points fixed to v4 centers; turns/elevations are this build's choices.
route('C01_Mountain_Main',[(-60,-376,16),(-60,-328,16),(-48,-292,13),(-32,-250,10),(-28,-205,7),(-12,-160,4),(0,-120,1.7),(0,-80,0),(0,-18,0)],6)
route('C01_Mountain_Rescue_Bypass',[(-60,-355,16),(-82,-349,16),(-82,-323,16),(-66,-306,14.2),(-48,-292,13)],4,'permanent_bypass')
route('C02_Gate_Plaza',[(0,-80,0),(0,-18,0),(0,-4,0)],5)
route('C03_Plaza_Training',[(-15,0,0),(-22,4,0),(-29,4,0)],4)
route('C04_Plaza_Academy',[(15,0,0),(22,6,0),(28,6,0)],4)
route('C05_Plaza_Shop',[(-12,-12,0),(-20,-30,0),(-25,-30,0)],4)
route('C06_Plaza_Inn',[(12,-12,0),(24,-32,0),(34,-32,0)],4)
route('C07_Town_Forest',[(-17,0,0),(-17,-16,0),(-60,-16,0),(-90,0,0),(-135,-8,1),(-175,-8,3),(-216,0,4),(-248,-8,4),(-272,0,4)],6)
route('Forest_Loop_South',[(-248,-8,4),(-277,-40,4),(-316,-36,4),(-328,-10,4),(-328,23,4)],4,'permanent_bypass')
route('Forest_Loop_North',[(-328,29,4),(-328,56,4),(-296,66,4),(-248,48,4),(-216,0,4)],4,'permanent_bypass')
route('Forest_Bridge',[(-328,23,4),(-328,29,4)],4,'bridge',skip=[0])
route('Forest_Log_Shortcut',[(-272,0,4),(-282,14,4),(-300,14,4),(-328,5,4)],4,'shortcut')
route('C08_Town_Waterworks',[(17,0,0),(20,-8,0),(62,-8,0),(90,0,0),(138,-12,.5),(187,-22,1.4),(226,-42,2),(270,-42,2)],6)
route('WW_West_Permanent_Maintenance',[(270,-42,2),(243,-42,2),(243,34,2),(270,34,2)],4,'permanent_bypass')
route('WW_East_Return',[(270,34,2),(270,30,2),(299,30,2),(299,-42,2),(270,-42,2)],4,'return')
route('WW_Entry_Approach',[(270,-42,2),(270,-18,2)],4)
route('WW_Lower_Deck',[(270,-18,2),(270,-12,2)],4,'bridge',skip=[0])
route('WW_Middle_Approach',[(270,-12,2),(270,2,2)],4)
route('WW_Upper_Approach',[(270,8,2),(270,22,2)],4)
route('WW_Upper_Deck',[(270,22,2),(270,28,2)],4,'bridge',skip=[0])
route('WW_Control',[(270,28,2),(270,34,2)],4)
route('WW_Craftsman_Access',[(299,-7,2),(290,-7,2)],4)
route('WW_Optional_East_Gate',[(299,14,2),(290,14,2)],3,'optional_mechanical_gate')
route('C09_Town_Abbey',[(0,18,0),(0,90,0),(-16,135,4),(-10,178,10.8),(0,190,12),(0,204,12),(0,234,12)],6)
route('Abbey_Water_Approach',[(0,234,12),(0,237,12)],6)
route('Abbey_Bridge',[(0,237,12),(0,243,12)],4,'bridge',skip=[0])
route('C10_Abbey_Hall',[(0,243,12),(0,264,12),(5,280,12),(5,284,12),(-5,289,12),(-10,299,12)],5)
route('Abbey_Permanent_West_Bypass',[(0,220,12),(-57,220,12),(-57,255,12),(-12,266,12),(0,264,12)],4,'permanent_bypass')
route('Abbey_Return_East',[(24,302,12),(31,302,12),(31,257,12),(0,248,12)],4,'return')
route('C11_Waterworks_Relay',[(299,30,2),(305,80,3),(290,124,9),(280,174,16),(250,190,16)],6)
route('C12_Relay_Abbey',[(220,220,16),(175,248,16),(110,260,13),(60,245,12),(60,229,12),(0,229,12)],6)
route('Relay_Invasion_North',[(250,246,16),(264,264,16),(278,282,18)],4,'invasion')
route('Relay_Invasion_East',[(276,220,16),(296,234,16),(316,241,18)],4,'invasion')
route('Relay_Entry_Link',[(250,190,16),(250,192,16)],6)
# Closed outer loop is a real route surface, not only an annotation.
ring=[(250+28*math.cos(t*2*math.pi/64),220+28*math.sin(t*2*math.pi/64),16) for t in range(65)];route('Relay_Outer_Ring',ring,5,'loop')
route('Relay_Core_South',[(250,192,16),(250,210,16)],5)
route('Relay_Core_West',[(222,220,16),(240,220,16)],5)
# Additional explicit service and gameplay access spurs.
for n,pts,w in [('Forest_Fire1_Access',[(-248,-8,4),(-252,-19,4)],4),('Forest_Fire2_Access',[(-282,14,4),(-282,22,4)],4),('Forest_Fire3_Access',[(-296,66,4),(-305,53,4)],4),('Forest_Shelter_Access',[(-304,63.5,4),(-308,67,4),(-311,67,4.35),(-313.9,67,4.35)],3),('SCN01_Shelter_Access',[(-60,-345,16),(-65,-345,16),(-66.6,-345,16.35)],4)]:route(n,pts,w,'local_access')
# Surface stamps are intentional flat functional platforms, feathered into a single continuous terrain mesh.
stamps=[('town',0,0,97,97,0,40),('entry',-65,-342,29,24,16,35),('forest',-270,0,70,90,4,35),('waterworks',270,0,65,75,2,35),('abbey',0,270,85,80,12,35),('relay',250,220,45,45,16,35)]
# Terrain channels: actual depressions. Their water surfaces remain separate objects.
channels=[(-340,-279,24,28,2.8,3.3), (247,295,-17,-13,.6,1.25),(247,295,3,7,.6,1.25),(247,295,23,27,.6,1.25),(-40,45,238,242,10.5,11.1)]
segments=[]
for rr in routes:
 for j,(p,q) in enumerate(zip(rr['points_m'],rr['points_m'][1:])):
  if rr['kind']=='bridge':continue
  x,y,z=p;dx=q[0]-x;dy=q[1]-y;segments.append((x,y,z,dx,dy,q[2]-z,dx*dx+dy*dy,rr['width_m']))

def distseg(x,y,seg):
 a,b,z,dx,dy,dz,l,w=seg;t=max(0,min(1,((x-a)*dx+(y-b)*dy)/l));return math.hypot(x-a-t*dx,y-b-t*dy),z+t*dz,w

def ground(x,y):
 # Broad rolling valley with a higher rocky rim inside the same 800m footprint.
 z=3+2*math.sin(x/90)*math.cos(y/110)+.00005*(x*x+y*y)
 z += 28*math.exp(-((x+360)/75)**2-((y+210)/150)**2)+23*math.exp(-((x-345)/80)**2-((y-330)/80)**2)+27*math.exp(-((x+100)/200)**2-((y-383)/58)**2)
 for n,cx,cy,hx,hy,h,feather in stamps:
  dd=max(abs(x-cx)-hx,abs(y-cy)-hy,0);f=max(0,1-dd/feather)
  if f:z=z*(1-f)+h*f
 best=(1e9,0,0)
 for sg in segments:
  dd,zz,ww=distseg(x,y,sg);d=dd-ww/2
  if d<best[0]:best=(d,zz,ww)
 if best[0]<5:
  f=max(0,min(1,(5-best[0])/5));z=z*(1-f)+(best[1]-.09)*f
 for x0,x1,y0,y1,bottom,waterz in channels:
  if x0<=x<=x1 and y0<=y<=y1:z=bottom
 return z
# Add channel edges to the regular 4m terrain grid so channel banks are actual edges.
xs=sorted(set(range(-400,401,4))|{v for ch in channels for v in ch[:2]});ys=sorted(set(range(-400,401,4))|{v for ch in channels for v in ch[2:4]});vs=[(x,y,ground(x,y)-.03) for y in ys for x in xs];nx=len(xs);fs=[]
for j in range(len(ys)-1):
 for i in range(nx-1):k=j*nx+i;fs.append((k,k+1,k+nx+1,k+nx))
terr=mesh('WORLD_Terrain_Continuous_800x800m',vs,fs,earth,C['00_CONTINUOUS_TERRAIN'],'terrain');terr['extent_m']=[800,800];terr['production_status']='geographic_blockout';terr['orientation']='X east; Y north; Z up'
# Solid dark border skirt provides an honest model edge, not another playable continent.
for n,p,q in [('south',(-400,-400),(400,-400)),('north',(-400,400),(400,400)),('west',(-400,-400),(-400,400)),('east',(400,-400),(400,400))]:
 vv=[];ff=[]
 for i in range(201):
  x=p[0]+(q[0]-p[0])*i/200;y=p[1]+(q[1]-p[1])*i/200;vv += [(x,y,ground(x,y)-.03),(x,y,-15)]
  if i:ff.append((2*i-2,2*i,2*i+1,2*i-1))
 mesh('WorldBoundary_'+n,vv,ff,earth,C['00_CONTINUOUS_TERRAIN'],'world_boundary')
for i,(x0,x1,y0,y1,b,w) in enumerate(channels):
 floor('CHANNEL_'+str(i)+'_Bed',(x0+x1)/2,(y0+y1)/2,b,x1-x0,y1-y0,C['00_CONTINUOUS_TERRAIN'],dark)
 box('CHANNEL_'+str(i)+'_Water',((x0+x1)/2,(y0+y1)/2,w-.03),(x1-x0,y1-y0,.06),water,C['00_CONTINUOUS_TERRAIN'],'water_proxy')
 # Banks are solid, top at neighboring region level. Kept outside the channel and bridge approaches.
 h=4 if i==0 else 2 if i<4 else 12
 for yy in [y0-.25,y1+.25]:box('CHANNEL_'+str(i)+'_RetainingBank'+str(yy),((x0+x1)/2,yy,(b+h)/2),(x1-x0,.5,h-b),stone,C['00_CONTINUOUS_TERRAIN'],'bank')
# Stage 1 checkpoint protects the continuous geography and circulation before scene contents.
print('CHECKPOINT_TERRAIN_READY',len(bpy.data.objects),flush=True)
bpy.context.view_layer.update()
bpy.ops.wm.save_as_mainfile(filepath=ROOT+'/stages/01_Terrain_And_Routes.blend',compress=True)
print('CHECKPOINT_TERRAIN_SAVED',flush=True)

# Reuse standard paving only on flat town / local straight runs; no per-instance stretches.
def pave(n,pts,col):
 for i,(x,y,z,rot) in enumerate(pts):
  inst('KIT_Path_Flagstone_4x4_A',n+f'_{i:03d}',(x,y,z+.035),rot,col,'walkable_paving')
# Pavement effective surface is around local -0.035.
pave('Town_South_Paving',[(0,y,0,0) for y in range(-78,-18,4)],C['02_TOWN_SCN02_07'])
pave('Town_North_Paving',[(0,y,0,0) for y in range(18,90,4)],C['02_TOWN_SCN02_07'])
pave('Town_West_Paving',[(x,-16,0,-math.pi/2) for x in range(-60,-18,4)],C['02_TOWN_SCN02_07'])
pave('Town_East_Paving',[(x,-8,0,-math.pi/2) for x in range(22,62,4)],C['02_TOWN_SCN02_07'])
pave('SCN01_Local_Paving',[(-60,y,16,0) for y in range(-372,-328,4)],C['03_MOUNTAIN_SCN01'])

def barrier(n,p,q,col,which='wall'):
 p=Vector(p);q=Vector(q);d=q-p;L=d.length;count=int(L/4);u=d.normalized();ang=math.atan2(-u.x,u.y)
 key='KIT_WallLow_Run_4m' if which=='wall' else 'KIT_Fence_Rails_4m';post='KIT_WallLow_EndCornerPier' if which=='wall' else 'KIT_Fence_SharedPost'
 for i in range(count):inst(key,n+f'_Run{i}',p+u*4*i,ang,col,'boundary')
 for i in range(count+1):inst(post,n+f'_Post{i}',p+u*4*i,ang,col,'boundary')
# Boundary segments intentionally stop before gates and local paths.
town=C['02_TOWN_SCN02_07']
for x0,x1,y in [(-72,-12,-80),(16,72,-80),(-72,-12,80),(12,72,80)]:barrier('Town_Boundary'+str((x0,y)),(x0,y,0),(x1,y,0),town)
for x in [-80,80]:
 for y0,y1 in [(-68,-12),(12,68)]:barrier('Town_Boundary'+str((x,y0)),(x,y0,0),(x,y1,0),town)
print('TOWN_PAVING_COMPLETE',flush=True)
bpy.context.view_layer.update()
print('TOWN_PAVING_COMPLETE_UPDATED',flush=True)
# SCN02 plaza is an actual 35m diameter floor, not forced to the road tile grid.
cyl('SCN02_Plaza_35m',(0,0,-.20),17.5,.4,stone,town,64,'walkable_floor');cyl('SCN02_PROP05_LowHearth_Base',(0,0,.35),1.6,.7,stone,town);cyl('SCN02_PROP05_Hearth',(0,0,1.05),.70,.70,copper,town);cyl('SCN02_PROP05_WarmLamp',(0,0,1.50),.35,.30,amber,town)
cyl('SCN02_Well_Rim',(6,5,.45),1.35,.9,stone,town);cyl('SCN02_Well_Water',(6,5,.91),.95,.02,water,town,32,'water_proxy');box('SCN02_Drinking_Trough',(9,4,.45),(1,3,.9),stone,town);box('SCN02_Trough_Water',(9,4,.88),(.75,2.7,.03),water,town,'water_proxy')
print('SCN03 modest_START',flush=True)
bpy.context.view_layer.update()
# SCN03 modest gate with measured clear hole 5x6m, literal open cargo side passage.
for x in [-4,4]:box('SCN03_Gate_Pier'+str(x),(x,-80,3),(3,3.6,6),stone,town,'architecture_wall')
box('SCN03_Gate_Lintel',(0,-80,6.5),(11,3.6,1),stone,town,'architecture_wall');roof('SCN03_Gate_Roof',0,-80,7,12.4,5.4,1.5,town)
floor('SCN03_Cargo_SideRoad',11,-77,0,5,16,town,roadmat)
floor('SCN03_Registration_Pad',-10,-72,0,6,6,town)
for x in [-12.6,-7.4]:
 for y in [-74.6,-69.4]:box('SCN03_Canopy_Post'+str((x,y)),(x,y,1.7),(.24,.24,3.4),wood,town,'architectural_blockout')
roof('SCN03_Registration_Canopy',-10,-72,3.4,6,6,1.1,town)
box('SCN03_Registration_Counter',(-10,-72,.52),(3.6,1,.98),wood,town)
for i,(dx,ma) in enumerate([(-.8,dry),(0,copper),(.8,dark)]):box('SCN03_Register_Book_Seal_Mat'+str(i),(-10+dx,-72,1.05),(.45,.4,.1),ma,town)
humanoid('SCN03_Clerk_Placeholder',(-10,-70.5,0),town)
print('SCN04 measured_START',flush=True)
bpy.context.view_layer.update()
# SCN04 measured net activity volume 24x20x6m.
room('SCN04_TrainingHall',-36,4,0,24,20,6,town,'east')
for i,x in enumerate([-42,-31]):
 cyl('SCN04_Training_Station'+str(i),(x,2,.03),3,.06,dry,town,32,'walkable_floor')
 box('SCN04_PROP08_Dummy'+str(i),(x,4,1.05),(.45,.45,2.1),wood,town);box('SCN04_Dummy_Arms'+str(i),(x,4,1.2),(1.5,.25,.25),wood,town)
floor('SCN04_DemonstrationZone',-36,10,.02,6,3,town,dry);box('SCN04_PROP09_WeaponRack',(-46,11,1),(2,.7,2),wood,town);humanoid('SCN04_Demonstrator',(-36,10,.02),town)
print('SCN05 greenhouse_START',flush=True)
bpy.context.view_layer.update()
# SCN05 greenhouse and three separated material stations with a broad common aisle.
room('SCN05_Academy',35,6,0,22,18,4.6,town,'west','glass')
for i,(y,ma) in enumerate([(0,wood),(6,copper),(12,water)]):
 box('SCN05_PROP10_Table'+str(i),(39,y,.5),(4,2,1),dry,town)
 box('SCN05_Material_'+['WOOD','METAL','WATER'][i],(39,y,1.2),(2.5,1,.4),ma,town)
 if i==1:
  for xx in [38,40]:cyl('SCN05_Insulator'+str(xx),(xx,y,1.06),.16,.15,plaster,town)
humanoid('SCN05_Aisle_Scale',(31,6,0),town)
print('SCN06 half_START',flush=True)
bpy.context.view_layer.update()
# SCN06 half open store. No front wall: customer and production spaces stay distinct.
floor('SCN06_ShopFloor',-34,-31,0,20,14,town)
for yy in [-38,-24]:box('SCN06_SideWall'+str(yy),(-34,yy,2),(20,.5,4),plaster,town,'architecture_wall')
box('SCN06_BackWall',(-44,-31,2),(.5,14,4),plaster,town,'architecture_wall');roof('SCN06_Roof',-34,-31,4.2,22,16,1.7,town)
box('SCN06_TransactionCounter',(-29,-30,.55),(1.2,6,1.1),wood,town);box('SCN06_Workbench',(-38,-34,.55),(4,1.6,1.1),wood,town);box('SCN06_CoolingTrough',(-38,-27,.50),(3,1.8,1),stone,town);box('SCN06_CoolingWater',(-38,-27,.95),(2.7,1.5,.04),water,town,'water_proxy');humanoid('SCN06_Customer',(-25.5,-30,0),town)
print('SCN07 exact_START',flush=True)
bpy.context.view_layer.update()
# SCN07 exact 18x16 court, four spots and separated NPC wait positions.
floor('SCN07_Courtyard_18x16',35,-33,0,18,16,town);room('SCN07_Inn_Rear',35,-18,0,18,10,5.5,town,'west')
for x in [31.5,38.5]:
 for y in [-35.5,-30.5]:humanoid('SCN07_PartySlot'+str((x,y)),(x,y,0),town)
for x in [28,42]:humanoid('SCN07_NPC_Wait'+str(x),(x,-26,0),town)
cyl('SCN07_PROP19_LowRestLamp',(35,-23,.4),.9,.8,amber,town);box('SCN07_PROP07_NoticeBoard',(43,-36,1.2),(.2,3,2.4),wood,town)
barrier('SCN07_CourtSideWest_South',(26,-41,0),(26,-37,0),town,'fence')
barrier('SCN07_CourtSideWest_North',(26,-29,0),(26,-25,0),town,'fence')
barrier('SCN07_CourtSideEast',(44,-41,0),(44,-25,0),town,'fence')
print('Background houses_START',flush=True)
bpy.context.view_layer.update()
# Background houses reuse the original whole house geometry at original scale.
for i,(x,y,rot) in enumerate([(-60,-49,0),(-56,-64,.3),(-37,-64,0),(-15,-61,0),(24,-62,0),(44,-62,0),(63,-48,-.2),(66,-27,math.pi/2),(63,18,math.pi/2),(60,40,math.pi/2),(40,59,math.pi),(22,66,math.pi),(-24,64,math.pi),(-45,56,math.pi),(-65,35,-math.pi/2),(-65,16,-math.pi/2)]):inst('SRC_TownHouse_Exact',f'TOWN_ReusedHouse{i:02d}',(x,y,0),rot,town,'background_building')
print('TOWN_COMPLETE',flush=True)
bpy.context.view_layer.update()
print('TOWN_COMPLETE_UPDATED',flush=True)
# Original local SCN01 preserved as a small embedded pocket, never enlarged into the whole world.
entry=C['03_MOUNTAIN_SCN01'];inst('SRC_Shelter_Exact','SCN01_OriginalShelter_Retained',(-69,-345,16),0,entry,'retained_local_source')
inst('SRC_AccidentCart_Exact','SCN01_PROP20_AccidentCart',(-54,-335,16),0,entry);inst('SRC_Barrel_Exact','SCN01_PROP01_WaterBarrel',(-55,-337,16.9),0,entry);inst('SRC_RescuePlatform_Exact','SCN01_Rescue_Platform',(-55,-336,16),0,entry)
inst('SRC_ShelteredFire_Exact','SCN01_Sheltered_Fire',(-54,-335,16.65),0,entry,'fire_visual_proxy');box('SCN01_PROP02_Crate',(-67,-344,16.4),(.8,.8,.8),wood,entry)
for i in range(2):box('SCN01_Supply'+str(i),(-68+i*.7,-345,16.35),(.4,.5,.3),dry,entry)
for i,(x,y) in enumerate([(-63,-371),(-45,-293)]):box('SCN01_PROP06_Sign'+str(i),(x,y,ground(x,y)+1.1),(.16,.16,2.2),wood,entry);box('SCN01_SignBoard'+str(i),(x,y,ground(x,y)+1.8),(1.8,.16,.55),wood,entry)
# Solid gentle rescue approach, no magic/climbing prerequisite.
mesh('SCN01_RescueRamp',[(-58,-340,16),(-54,-340,16),(-54,-337,16.9),(-58,-337,16.9),(-58,-340,15.8),(-54,-340,15.8),(-54,-337,15.8),(-58,-337,15.8)],[(0,1,2,3),(4,7,6,5),(0,4,5,1),(1,5,6,2),(2,6,7,3),(3,7,4,0)],wood,entry,'walkable_floor')
for x in [-64,-56]:barrier('SCN01_Rail'+str(x),(x,-368,16),(x,-360 if x==-64 else -352,16),entry,'fence')
humanoid('SCN01_DesignScale165',(-60,-348,16),entry)
# Shared maintenance bridge assembly uses v3 fixed 4m deck + 1m abutments, all instances scale1.
def bridge(n,x,y,z,col,rot=0):
 def transform(px,py,pz=0):return (x+px*math.cos(rot)-py*math.sin(rot),y+px*math.sin(rot)+py*math.cos(rot),z+.035+pz)
 for off in [0,5]:inst('KIT_Bridge_Abutment_6x1m',n+f'_Bank{off}',transform(0,off),rot,col,'walkable_bridge_bank')
 for i in range(16):inst('KIT_Bridge_DeckPlank_4x0p25',n+f'_Plank{i}',transform(0,1+i*.25),rot,col,'walkable_bridge')
 for px in [-1.75,1.75]:
  inst('KIT_Bridge_Bearer_4p5m',n+'_Bearer'+str(px),transform(px,1),rot,col,'bridge_support');inst('KIT_Fence_Rails_4m',n+'_Rail'+str(px),transform(px,1),rot,col,'boundary')
  for yy in [1,3,5]:inst('KIT_Fence_SharedPost',n+'_Post'+str((px,yy)),transform(px,yy,-.14),rot,col,'boundary')
print('ENTRY_COMPLETE',flush=True)
bpy.context.view_layer.update()
print('ENTRY_COMPLETE_UPDATED',flush=True)
# Forest core: three separated designated fires, clear banks, rescue shed, ordinary trees differentiated from supports.
forest=C['04_FOREST_SCN08'];bridge('SCN08_MaintenanceBridge',-328,23,4,forest)
for i,(x,y) in enumerate([(-252,-23),(-282,26),(-306,50)]):
 floor('SCN08_FireStationPad'+str(i),x,y,4,8,8,forest,earth);cyl('SCN08_DesignatedFire_'+str(i+1),(x,y,4.04),1.3,.08,dark,forest);inst('SRC_ShelteredFire_Exact','SCN08_FireVisual'+str(i),(x,y,4.1),0,forest,'fire_visual_proxy');box('SCN08_Fuel'+str(i),(x,y,4.3),(1.5,1,.6),wood,forest)
inst('SRC_Shelter_Exact','SCN08_Cargo_Shed',(-317,67,4),0,forest);inst('SRC_Barrel_Exact','SCN08_RescueWater',(-312,72,4),0,forest)
# Fallen trunk is reused exact local geometry, rotated to span the shortcut with >2.7m underside.
log=inst('SRC_Trunk','SCN08_FallenLog_OverShortcut',(-291,17.1,7),0,forest,'overhead_obstacle');log.rotation_euler.x=math.pi/2
for yy in [11,17]:box('SCN08_PROP15_BurnableSupport'+str(yy),(-291,yy,5.45),(.45,.45,2.9),copper,forest)
floor('SCN08_Cargo_SafePad',-312.9,67,4.35,3.8,4,forest)
humanoid('SCN08_RescuePlaceholder',(-312.5,67,4.35),forest)
print('FOREST_COMPLETE',flush=True)
bpy.context.view_layer.update()
print('FOREST_COMPLETE_UPDATED',flush=True)
# Waterworks core markers and permanent mechanical route.
ww=C['05_WATERWORKS_SCN09'];floor('SCN09_EntryObservation',270,-42,2,14,10,ww);bridge('SCN09_LowerBridge',270,-18,2,ww);bridge('SCN09_UpperBridge',270,22,2,ww)
floor('SCN09_ControlPlatform',270,34,2,22,10,ww);floor('SCN09_CraftsmanPlatform',289,-7,2,10,8,ww);box('SCN09_RescuePushableObstacle',(285,-7,2.7),(1,1.5,1.4),wood,ww);inst('SRC_Barrel_Exact','SCN09_LimitedWaterBarrel',(292,-10,2),0,ww);humanoid('SCN09_Craftsman',(290,-5,2),ww)
# Three stepping piers are optional, never the sole way across.
for i,x in enumerate([263,270,277]):cyl('SCN09_StonePier'+str(i+1),(x,5,1.5),1.2,1,stone,ww,12,'optional_step')
cyl('SCN09_LowPressureTower',(276,35,5),2.5,6,stone,ww);cyl('SCN09_TowerCopperRing',(276,35,6.5),2.7,.35,copper,ww)
wheel=cyl('SCN09_Waterwheel_Proxy',(290,37,4.3),2.4,.5,wood,ww,20);wheel.rotation_euler.x=math.pi/2
for i,(x,y) in enumerate([(286,-15),(286,25)]):
 box('SCN09_PROP11_Gate'+str(i+1),(x,y,1.55),(4,.3,1.6),copper,ww)
 for dx in [-2.2,2.2]:box('SCN09_GateRail'+str((i,dx)),(x+dx,y,2.5),(.2,.35,3.4),stone,ww)
 cyl('SCN09_GateHandwheel'+str(i),(x+3,y-2,2.8),.6,.15,copper,ww).rotation_euler.x=math.pi/2
for i,(x,y) in enumerate([(296,-10),(296,12)]):
 box('SCN09_PROP12_Switch'+str(i+1),(x,y,2.8),(.5,.7,1.6),copper,ww)
for x,y,name in [(291,-24,'Source'),(296,17,'Receiver')]:
 for dx in [-.8,.8]:cyl('SCN09_'+name+'_Insulator'+str(dx),(x+dx,y,2.25),.25,.5,plaster,ww)
 box('SCN09_Electric_'+name,(x,y,2.8),(2,1,1),proxy,ww)
line('SCN09_Conductor_Separated',[(291,-24,2.06),(296,-24,2.06),(296,-10,2.06),(296,12,2.06),(296,17,2.06)],.06,copper,ww)
# Separate optional electrically operated east gate. The permanent return remains outside it.
for yy in [11,17]:box('SCN09_EastGate_Jamb'+str(yy),(295,yy,4),(.5,.5,4),stone,ww,'architecture_wall')
box('SCN09_EastGate_Lintel',(295,14,6.2),(.6,6.5,.4),stone,ww,'architecture_wall')
box('SCN09_EastGate_OpenLeaf',(292.5,11,3.7),(5,.18,3.4),copper,ww)
box('SCN09_EastGate_Motor',(296.4,17.5,2.6),(.5,.8,1.2),proxy,ww)
line('SCN09_Receiver_To_GateMotor',[(296,17,2.9),(296.4,17,2.9),(296.4,17.5,2.9)],.055,copper,ww)
# Raised optional dropbridge has visible hinge and two actual anchor endpoints. Mechanism is a proxy.
for i in range(16):
 o=inst('KIT_Bridge_DeckPlank_4x0p25',f'SCN09_OptionalDropBridgePlank{i}',(270,3,2.035),0,ww,'mechanical_bridge_proxy');o.rotation_euler.x=math.radians(72);o.location.y+=math.cos(math.radians(72))*(i*.25);o.location.z+=math.sin(math.radians(72))*(i*.25)
for x in [268.5,271.5]:
 box('SCN09_AnchorPost'+str(x),(x,2,4),(.35,.35,4),wood,ww)
 line('SCN09_PROP16_Rope'+str(x),[(x,2,5.9),(x,3+4*math.cos(math.radians(72)),2+4*math.sin(math.radians(72)))],.055,copper,ww)
 anchor('SCN09_RopeAnchor_Fixed'+str(x),(x,2,5.9));anchor('SCN09_RopeAnchor_Bridge'+str(x),(x,4.236,5.804))
box('SCN09_BridgeHinge',(270,3,2.08),(4.4,.25,.25),copper,ww)
# Independent collecting pool with readable water connection to the north output.
floor('SCN09_PoolBottom',309,24,.6,14,18,ww,dark);box('SCN09_PoolWater',(309,24,1.23),(14,18,.04),water,ww,'water_proxy')
for x in [302,316]:box('SCN09_PoolBank'+str(x),(x,24,1.3),(.5,18,1.4),stone,ww,'bank')
line('SCN09_Supply_Output_Flow',[(276,35,6.5),(309,35,4.8),(309,29,1.3)],.30,water,ww,'water_flow_proxy')
for xx in [290,309]:box('SCN09_OutputPipeSupport'+str(xx),(xx,35,3.35),(.35,.35,2.7),copper,ww)
for i,x in enumerate([263,276]):box('SCN09_EntryCrate'+str(i),(x,-46,2.55),(1.1,1.1,1.1),wood,ww)
print('WATERWORKS_COMPLETE',flush=True)
bpy.context.view_layer.update()
print('WATERWORKS_COMPLETE_UPDATED',flush=True)
# Abbey, 2 storeys plus modest broken bell tower, frontcourt and independent bypass.
abb=C['06_ABBEY_SCN10_11'];floor('SCN10_Forecourt',0,213,12,50,38,abb);bridge('SCN10_WaterBridge',0,237,12,abb)
room('SCN10_WestWing',-32,287,12,17,42,10.8,abb,'east');room('SCN10_EastWing',43,287,12,14,42,10.8,abb,'west')
# Low side colonnade along main approach keeps the middle route readable.
for x in [-10,13]:
 for y in [247,254,261,268]:box('SCN10_Colonnade'+str((x,y)),(x,y,15),(.8,.8,6),stone,abb,'architecture_wall')
 box('SCN10_ColonnadeLintel'+str(x),(x,257.5,18.3),(1.2,24,.6),stone,abb,'architectural_blockout')
# Bell tower is 19m above abbey, slightly higher than 8–10m source trees, not a huge new city.
for x in [-50,-43]:
 for y in [300,307]:box('SCN10_BellTowerPier'+str((x,y)),(x,y,20.5),(1.3,1.3,17),stone,abb,'architecture_wall')
box('SCN10_BellTowerUpper',(-46.5,303.5,28.7),(8.3,8.3,.7),stone,abb);roof('SCN10_BrokenBellRoof',-46.5,303.5,29.2,8.6,8.6,1.8,abb)
cyl('SCN10_BellProxy',(-46.5,303.5,26.2),1.8,2.4,copper,abb);box('SCN10_ReturnShortcutSwitch',(34,258,13),(.4,.6,2),copper,abb);floor('SCN10_SafePreparation',-15,226,12,12,8,abb,dry)
# SCN11 clear 38m hall, continuous 5m outer circulation band, obstacles contained inside r<=12.5m.
cyl('SCN11_HallFloor_38m',(5,302,11.75),19,.5,stone,abb,96,'walkable_floor')
# Perimeter piers/walls beyond floor, leave 7m south and 7m side openings.
for i in range(32):
 t=2*math.pi*i/32;x=5+19.5*math.cos(t);y=302+19.5*math.sin(t)
 if abs(x-5)<4 and y<290 or abs(y-302)<4:continue
 o=box('SCN11_OuterWall'+str(i),(x,y,14),(3.5,.55,4),stone,abb,'architecture_wall');o.rotation_euler.z=t+math.pi/2
for i,t in enumerate([math.radians(30),math.radians(150),math.radians(270)]):
 x=5+11.5*math.cos(t);y=302+11.5*math.sin(t);cyl('SCN11_MainPillar'+str(i+1),(x,y,16),1,8,stone,abb,16,'architecture_wall')
cyl('SCN11_LowFurnace',(5,302,12.6),3.4,1.2,stone,abb);cyl('SCN11_FurnaceCore',(5,302,13.4),1.4,.5,copper,abb)
for x in [1,9]:box('SCN11_BrokenBellSupport'+str(x),(x,308,17),(.75,.75,10),stone,abb,'architecture_wall')
box('SCN11_BellSupportBeam',(5,308,21.5),(9,.8,.7),wood,abb);cyl('SCN11_BrokenBell',(5,308,19.3),2.5,3,copper,abb)
for i,x in enumerate([-19,29]):
 floor('SCN11_WaterPlatform'+str(i+1),x,302,12,10,10,abb);sx=x+1 if x<0 else x;box('SCN11_WaterSupply'+str(i+1),(sx,305,12.65),(3,2,1.3),stone,abb);box('SCN11_WaterSurface'+str(i+1),(sx,305,13.2),(2.6,1.6,.04),water,abb,'water_proxy');line('SCN11_SupplyPipe'+str(i),[(sx,305,12.05),(sx,313,12.05),(5,313,12.05),(5,308,12.05)],.12,copper,abb)
route('Hall_Rear_Maintenance',[(-19,302,12),(-19,298,12),(-21,298,12),(-21,329,12),(33,329,12),(33,298,12),(29,298,12),(29,302,12)],3,'permanent_bypass')
# Maintenance route built after terrain uses explicit continuous backing.
for p,q in zip(routes[-1]['points_m'],routes[-1]['points_m'][1:]):
 aa=Vector(p);bb=Vector(q);mid=(aa+bb)/2;o=box('SCN11_MaintenanceBackfill'+str(p),(mid.x,mid.y,11.5),(3,(bb-aa).length,1),earth,abb,'route_bed');o.rotation_euler.z=math.atan2(-(bb.x-aa.x),bb.y-aa.y)
box('SCN11_PROP18_RewardCrate',(5,313,12.5),(1,.8,1),wood,abb);cyl('SCN11_PROP17_AncientSeal',(5,313,13.08),.3,.15,copper,abb);humanoid('SCN11_DesignScale165',(-10,294,12),abb)
print('ABBEY_COMPLETE',flush=True)
bpy.context.view_layer.update()
print('ABBEY_COMPLETE_UPDATED',flush=True)
# Relay 12m tower plus 2 rods, 4 low covers, a safe dry stand, separate conductor contact boundary.
rel=C['07_RELAY_SCN12'];cyl('SCN12_CorePlatform',(250,220,15.8),9,.4,stone,rel,48,'walkable_floor');cyl('SCN12_PROP22_Tower',(250,220,21.75),1.6,11.5,stone,rel)
for z,r in [(17,2.3),(21,2.1),(25,1.9),(27.7,1.65)]:cyl('SCN12_TowerRing'+str(z),(250,220,z),r,.3,copper,rel)
cyl('SCN12_CoreBeacon',(250,220,27.75),.75,.5,amber,rel)
for i,x in enumerate([238,262]):
 cyl('SCN12_PROP14_GroundRod'+str(i+1),(x,223,18),.2,4,copper,rel);cyl('SCN12_RodInsulator'+str(i),(x,223,16.4),.65,.8,plaster,rel);line('SCN12_Conductor'+str(i),[(250,220,16.05),(x,220,16.05),(x,223,16.05)],.07,copper,rel)
for i,(x,y) in enumerate([(238,208),(262,208),(238,232),(262,232)]):box('SCN12_LowCover'+str(i+1),(x,y,16.55),(4,1.4,1.1),stone,rel)
floor('SCN12_DrySafeStand',243,190,16,8,4,rel,dry);cyl('SCN12_RescueLamp',(240.5,190,16.6),.45,1.2,amber,rel);humanoid('SCN12_Scale165',(244,190,16),rel)
for n,x,y in [('North',278,282),('East',316,241)]:cyl('SCN12_InvasionArrival_'+n,(x,y,17.8),3,.4,earth,rel,24,'walkable_floor')
# Minimal shared Lab / Field / daily cache subpoints stay inside existing six regions.
for name,x,y,z,col in [('Lab',52,27,0,town),('Field',-137,128,ground(-137,128),C['08_CONTEXT_FOLIAGE']),('ForestSupplyCache',-261,42,4,forest),('WWSupplyCache',320,-30,2,ww),('AbbeySupplyCache',-65,216,12,abb)]:
 floor('SUBPOINT_'+name,x,y,z,6,6,col);box('SUBPOINT_'+name+'_Crate',(x,y,z+.5),(1,1,1),wood,col);inst('SRC_Barrel_Exact','SUBPOINT_'+name+'_Barrel',(x+1.8,y,z),0,col);anchor('SUBPOINT_'+name,(x,y,z))
print('RELAY_COMPLETE',flush=True)
bpy.context.view_layer.update()
print('RELAY_COMPLETE_UPDATED',flush=True)
# Reference trees/grass reused as actual shared mesh objects at unchanged scale. Keep routes and sites clear.
fol=C['08_CONTEXT_FOLIAGE']
def route_clear(x,y,margin=5):
 return all(distseg(x,y,sg)[0]>distseg(x,y,sg)[2]/2+margin for sg in segments)
def building_clear(x,y):
 if abs(x)<82 and abs(y)<82:return False
 if 225<x<322 and -53<y<55:return False
 if -61<x<62 and 201<y<336:return False
 if 210<x<291 and 181<y<261:return False
 if -88<x<-46 and -363<y<-321:return False
 return True
positions=[]
for i in range(1150):
 x=random.uniform(-382,382);y=random.uniform(-382,382)
 # More forest density; thinner geographic context everywhere else.
 if not (-343<x<-203 and -87<y<87) and random.random()>.33:continue
 if not route_clear(x,y,5) or not building_clear(x,y):continue
 if any(x0-3<x<x1+3 and y0-3<y<y1+3 for x0,x1,y0,y1,b,w in channels):continue
 z=ground(x,y);positions.append((x,y,z));inst('SRC_Tree_'+str(i%2),'FOL_Tree'+str(i),(x,y,z),random.random()*math.tau,fol)
for i in range(170):
 x=random.uniform(-341,-201);y=random.uniform(-85,85)
 if route_clear(x,y,4) and building_clear(x,y):inst('SRC_Tree_'+str(i%2),'FOREST_DenseTree'+str(i),(x,y,ground(x,y)),random.random()*math.tau,fol)
for i,(x,y,z) in enumerate(positions[::2]):
 for j in range(3):
  xx=x+random.uniform(-3,3);yy=y+random.uniform(-3,3)
  if route_clear(xx,yy,2):inst(['SRC_Shrub','SRC_Fern','SRC_Grass'][j],'FOL_Undergrowth'+str((i,j)),(xx,yy,ground(xx,yy)),random.random()*math.tau,fol)
# Rock reuse concentrates along valley sides / terrain edges without blocking route envelopes.
for i in range(125):
 x=random.uniform(-380,380);y=random.uniform(-380,380)
 if route_clear(x,y,7) and building_clear(x,y):inst('KIT_Rock_River_A','ROCK_Reused'+str(i),(x,y,ground(x,y)),random.random()*math.tau,fol)
# v3 rock/earth close edge and stairs at a bounded optional scenic perch, not main entry.
for k in range(4):inst('KIT_RockToe_Earth_2m','SCN01_RockToe'+str(k),(-56,-368+2*k,16.035),0,entry)
for k in range(4):inst('KIT_Rock_River_A','SCN01_RoadRock'+str(k),(-54,-368+2*k,16),0,entry)
inst('KIT_Stair_Solid_4m_Rise1m','SCN09_OptionalObservationStair',(319,-52,2.035),0,ww,'walkable_stair');inst('KIT_Ramp_Bank_4m_Rise1m','SCN09_ObservationStairEarth',(319,-52,2.035),0,ww,'route_bed');floor('SCN09_ObservationPerch',319,-46,3,6,4,ww)
# Unmodified legacy 65-bone reference alongside a 1.65m ruler, translation only.
root=bpy.data.objects.get('SCALE_REFERENCE_TRANSLATION_ONLY')
if root:root.location=(-61.7,-348,16.006743584759533);root['reference_scope']='old 1.8027965m unchanged; not target protagonist'
for o in character_collection.objects:o.hide_render=False;o.hide_set(False)
print('FOLIAGE_COMPLETE',flush=True)
bpy.context.view_layer.update()
print('FOLIAGE_COMPLETE_UPDATED',flush=True)
# Named scene anchors, fixed design footprint and adopted z.
scene_rows=[('SCN_01',(-65,-290),[80,180],16),('SCN_02',(0,0),[35,35],0),('SCN_03',(0,-80),[5,6],0),('SCN_04',(-36,4),[24,20],0),('SCN_05',(35,6),[22,18],0),('SCN_06',(-34,-31),[20,14],0),('SCN_07',(35,-33),[18,16],0),('SCN_08',(-270,0),[140,180],4),('SCN_09',(270,0),[130,150],2),('SCN_10',(0,270),[170,160],12),('SCN_11',(5,302),[38,38],12),('SCN_12',(250,220),[90,90],16)]
for n,p,d,z in scene_rows:
 o=anchor(n+'_CENTER',(p[0],p[1],z),n);o['footprint_m']=d;o['position_scope']='v4 region center or adopted internal scene placement';scenes.append({'scene_id':n,'center_m':[p[0],p[1],z],'footprint_m':d})
# Annotation uses Blender text objects; this is an explicitly separate technical overview layer.
def text(n,body,p,size=5):
 cu=bpy.data.curves.new(n,'FONT');cu.body=body;cu.size=size;cu.align_x='CENTER';cu.materials.append(mark);o=bpy.data.objects.new(n,cu);ANN.objects.link(o);o.location=p;return o
for body,p in [('01 RAINFALL PATH',(-120,-305,110)),('02-07 EMBER TOWN',(0,-108,110)),('08 ASHWOOD',(-270,-111,110)),('09 WATERWORKS',(270,-93,110)),('10-11 BROKEN BELL ABBEY',(0,362,110)),('12 STORM RELAY',(250,284,110))]:text(body,body,p,8)
text('Title','AETHERLAB  |  CONTINUOUS WORLD BLOCKOUT',(-20,-395,110),11);text('Subtitle','800 x 800 m   /   6 REGIONS   /   12 SCENES   /   +X EAST, +Y NORTH',(-20,-380,110),6)
line('Scale100m',[(-365,-357,110),(-265,-357,110)],.7,mark,ANN,'annotation');text('Scale100','100 m',(-315,-350,110),6);line('NorthArrow',[(370,290,110),(370,360,110),(361,344,110),(370,360,110),(379,344,110)],1,mark,ANN,'annotation');text('North','N',(370,372,110),10)
# Cameras include orthographic evidence and natural 35mm standing-height views (relative height documented).
cam_specs=[]
def camera(n,pos,target,lens=35,ortho=None,groundz=None):
 ca=bpy.data.cameras.new(n);o=bpy.data.objects.new(n,ca);C['09_REVIEW_CAMERAS'].objects.link(o);o.location=pos;o.rotation_euler=(Vector(target)-o.location).to_track_quat('-Z','Y').to_euler();ca.lens=lens;ca.clip_end=3000
 if ortho:ca.type='ORTHO';ca.ortho_scale=ortho
 cam_specs.append({'camera':n,'position_m':list(pos),'target_m':list(target),'lens_mm':lens,'ortho_scale_m':ortho,'support_height_m':groundz,'camera_height_above_ground_m':pos[2]-groundz if groundz is not None else None});return o
s.camera=camera('CAM_Global_Axonometric',(680,-980,900),(0,0,5),ortho=1030)
camera('CAM_Global_Top',(0,0,1100),(0,0,0),ortho=850)
camera('CAM_Town_Overview',(102,-132,112),(0,-4,0),ortho=190)
camera('CAM_Waterworks_Overview',(351,-107,96),(271,0,2),ortho=137)
camera('CAM_Abbey_Overview',(120,158,138),(0,278,15),ortho=196)
camera('CAM_Forest_Overview',(-147,-138,128),(-275,12,4),ortho=192)
camera('CAM_Relay_Overview',(340,117,110),(250,220,16),ortho=131)
camera('CAM_Ground_SCN01',(-60,-354,17.55),(-61,-335,17.35),groundz=16)
camera('CAM_Ground_Gate',(1,-101,2.34),(0,-77,3.0),groundz=.79)
camera('CAM_Ground_Plaza',(1,-22,1.55),(0,3,1.9),groundz=0)
camera('CAM_Ground_Training',(-26,0,1.55),(-39,5,2),groundz=0)
camera('CAM_Ground_Academy',(27,3,1.55),(39,7,1.8),groundz=0)
camera('CAM_Ground_Shop',(-24.5,-34,1.55),(-38,-29,1.8),groundz=0)
camera('CAM_Ground_Inn',(26,-36,1.55),(36,-26,1.8),groundz=0)
camera('CAM_Ground_Forest',(-274,0,5.55),(-290,24,5.3),groundz=4)
camera('CAM_Ground_Waterworks',(270,-41,3.55),(270,27,4),groundz=2)
camera('CAM_Ground_Maintenance',(243,-24,3.55),(246,24,3.0),groundz=2)
camera('CAM_Ground_Abbey',(0,219,13.55),(-15,288,21),groundz=12)
camera('CAM_Ground_Hall',(-8,288,13.55),(5,305,16),groundz=12)
camera('CAM_Ground_Relay',(247,194,17.55),(250,220,20),groundz=16)
# Measure camera support from saved mesh-equivalent authored surfaces; relative eye height is explicit.
from mathutils.bvhtree import BVHTree
bpy.context.view_layer.update()
vv=[];ff=[]
for ob in s.objects:
 if ob.type!='MESH' or ob.get('role') not in ['terrain','walkable_floor','walkable_route','walkable_paving','walkable_bridge','walkable_bridge_bank','walkable_stair']:continue
 off=len(vv);vv += [ob.matrix_world@v.co for v in ob.data.vertices];ff += [tuple(off+j for j in p.vertices) for p in ob.data.polygons]
ground_bvh=BVHTree.FromPolygons(vv,ff,all_triangles=False)
for cs in cam_specs:
 if cs['support_height_m'] is None:continue
 cam=bpy.data.objects[cs['camera']];hit,normal,index,dist=ground_bvh.ray_cast(Vector((cam.location.x,cam.location.y,150)),Vector((0,0,-1)),400)
 if hit:
  cam.location.z=hit.z+1.55;cam.rotation_euler=(Vector(cs['target_m'])-cam.location).to_track_quat('-Z','Y').to_euler();cs['position_m']=list(cam.location);cs['support_height_m']=hit.z;cs['camera_height_above_ground_m']=1.55;cam['review_eye_height_above_surface_m']=1.55
# Physical modelling light. No UE runtime/weather behavior is implied.
li=bpy.data.lights.new('Overcast Sun','SUN');lo=bpy.data.objects.new('Overcast Sun',li);C['09_REVIEW_CAMERAS'].objects.link(lo);lo.rotation_euler=(.48,-.45,-.45);li.energy=2.2;li.angle=math.radians(30)
s.render.engine='CYCLES';s.cycles.samples=16;s.cycles.use_denoising=False;s.render.resolution_x=1600;s.render.resolution_y=1100;s.render.resolution_percentage=100;s.view_settings.view_transform='AgX';s.render.image_settings.file_format='PNG'
s['scope']='Full-world Blender geographic blockout only; not final art; no UE nav/collision/runtime/performance acceptance';s['source_kit_sha256']=hashlib.sha256(open(a.kit,'rb').read()).hexdigest();s['source_legacy_sha256']=hashlib.sha256(open(a.legacy,'rb').read()).hexdigest();s['extent_m']=[800,800];s['reference_image']='exec-8e127206-514a-42b0-b346-1432f2864d48.png; inspected before construction';s['target_character_height_m']=1.65;s['legacy_character_height_m']=1.8027965;s['phase']='GLOBAL BLOCKOUT; STOP BEFORE LOCAL POLISH'
# Default viewport opens into a useful global orthographic overview.
for screen in bpy.data.screens:
 for area in screen.areas:
  if area.type=='VIEW_3D':
   area.spaces.active.region_3d.view_distance=900;area.spaces.active.region_3d.view_location=(0,0,6);area.spaces.active.region_3d.view_rotation=s.camera.rotation_euler.to_quaternion();area.spaces.active.clip_end=4000;area.spaces.active.shading.type='MATERIAL'
print('FINAL_UPDATE_BEGIN',flush=True)
bpy.context.view_layer.update()
print('FINAL_UPDATE_END',flush=True)
bpy.data.orphans_purge(do_recursive=True)
print('PACK_BEGIN',len(bpy.data.images),flush=True)
bpy.ops.file.pack_all()
print('PACK_END',flush=True)
file=ROOT+'/source/AetherLab_Global_World_Blockout_v1.blend';bpy.ops.wm.save_as_mainfile(filepath=file,compress=True)
manifest={'version':1,'stage':'global geographic blockout','source_main':'f230dae39ea24406520c2622fd69987b033061f4','world_bounds_xy_m':[[-400,400],[-400,400]],'source_kit_sha256':s['source_kit_sha256'],'source_legacy_sha256':s['source_legacy_sha256'],'source_file':os.path.basename(file),'source_sha256':hashlib.sha256(open(file,'rb').read()).hexdigest(),'scenes':scenes,'routes':routes,'reused_sources':source_records,'instance_count':len(instances),'instances':instances,'specialist_placeholder_count':len(proxies),'specialist_placeholders':proxies,'cameras':cam_specs,'limitations':['Source geometric / visual proxies only, not gameplay state logic','No UE project compilation/testing/nav/collision/physics/performance acceptance','No local asset refinement / final UV / LOD / material acceptance','12 scenes do not add new regions; Lab/Field/daily points retain existing subpoint identity','Adopted z and route widths are this blockout only; not universal production grid']}
json.dump(manifest,open(ROOT+'/docs/World_Manifest.json','w'),ensure_ascii=False,indent=2)
json.dump({'sources':source_records,'instances':instances},open(ROOT+'/docs/Reuse_Statistics.json','w'),ensure_ascii=False,indent=2)
print('WORLD_SAVED',file,'REUSE',len(instances),'PROXIES',len(proxies),'MESHES',len(bpy.data.meshes))
