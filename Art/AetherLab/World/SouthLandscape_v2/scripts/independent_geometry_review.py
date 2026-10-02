"""Read-only independent saved-world geometry review. Run Blender -b --python this.py -- --source FILE --manifest JSON --output DIRECTORY.
Never executes the author's build module. Manifest polylines are query proposals only;
all support, clearance, dimensions and samples are read from reopened evaluated geometry.
"""
import bpy,json,math,sys,os,hashlib,collections,time,argparse
from pathlib import Path
from mathutils import Vector, Matrix
from mathutils.bvhtree import BVHTree
BASE=Path(__file__).resolve().parent.parent
DEFAULT_ROOT=BASE if (BASE/'source').exists() else BASE/'deliverables'
ap=argparse.ArgumentParser(description=__doc__)
ap.add_argument('--source',default=str(DEFAULT_ROOT/'source/AetherLab_Global_World_Blockout_v1.blend'))
ap.add_argument('--manifest',default=str(DEFAULT_ROOT/'docs/World_Manifest.json'))
ap.add_argument('--output',default=str(BASE/'review'))
args=ap.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
SRC,MAN,OUT=args.source,args.manifest,args.output
os.makedirs(OUT,exist_ok=True)
sha=lambda p:hashlib.sha256(open(p,'rb').read()).hexdigest()
source_sha=sha(SRC);bpy.ops.wm.open_mainfile(filepath=SRC);bpy.context.view_layer.update();dg=bpy.context.evaluated_depsgraph_get()
manifest=json.load(open(MAN));report={'reviewer_script_sha256':sha(__file__),'manifest_sha256':sha(MAN),'manifest_source_sha256':manifest.get('source_sha256'),'manifest_matches_source':manifest.get('source_sha256')==source_sha,'source':os.path.basename(SRC),'source_sha256':source_sha,'blender_version':bpy.app.version_string,'method':'Saved file reopened; evaluated per-object BVH, actual world transforms, positive and negative controls. No UE operations. Manifest route polylines specify probe locations only.','limits':['Discrete probes, not navigation/physics/continuous swept-volume proof','Walking envelope uses 0.30m radius, 1.803m height; three parallel traces at offsets up to -0.75,0,+0.75m, narrowed for declared narrow access links; feet allow <=0.22m authored-grade deviation','Markers representing characters are excluded as intended occupancy references','Low conduits <=0.20m above ground are treated as step-height candidates, not runtime-qualified traversable geometry']}
# Explicitly follow collection rendering hierarchy; hidden master sources do not count as world geometry.
active=set()
def visit(c,hidden=False):
 hidden=hidden or c.hide_render
 if not hidden:
  active.update(o.name for o in c.objects if not o.hide_render)
 for cc in c.children:visit(cc,hidden)
visit(bpy.context.scene.collection)
exclude=set()
for c in bpy.data.collections:
 if 'CHARACTER' in c.name or 'ANNOTATIONS' in c.name or 'GUIDES' in c.name:exclude.update(o.name for o in c.all_objects)
cache={};objs=[];byname={};grid=collections.defaultdict(list)
groundroles={'terrain','walkable_route','walkable_floor','walkable_paving','walkable_bridge','walkable_bridge_bank','walkable_stair','optional_step','bank'}
ignore_obstacle_roles={'water_proxy','water_flow_proxy','route_bed','world_boundary','annotation','scale_reference','fire_visual_proxy'}
for o in bpy.context.scene.objects:
 if o.name not in active or o.name in exclude or o.type not in {'MESH','CURVE','SURFACE','FONT'}:continue
 if o.get('role') in {'annotation','scale_reference'}:continue
 ev=o.evaluated_get(dg);me=ev.to_mesh();
 if not me or not len(me.vertices):
  ev.to_mesh_clear();continue
 key=(o.type,o.data.as_pointer(),str([(x.type,x.show_render) for x in o.modifiers]))
 if key not in cache:
  verts=[v.co.copy() for v in me.vertices];polys=[tuple(p.vertices) for p in me.polygons]
  cache[key]=BVHTree.FromPolygons(verts,polys,epsilon=1e-5),verts
 bv,vs=cache[key];mt=o.matrix_world.copy();pts=[mt@v for v in vs]
 low=Vector([min(v[k] for v in pts) for k in range(3)]);high=Vector([max(v[k] for v in pts) for k in range(3)])
 item={'name':o.name,'obj':o,'bvh':bv,'matrix':mt,'inv':mt.inverted(),'lo':low,'hi':high,'support':o.get('role') in groundroles,'compound_support':o.get('master') in {'SRC_Shelter_Exact','SRC_RescuePlatform_Exact'},'obstacle':o.get('role') not in groundroles|ignore_obstacle_roles}
 idx=len(objs);objs.append(item);byname[o.name]=item
 for gx in range(math.floor(low.x/8),math.floor(high.x/8)+1):
  for gy in range(math.floor(low.y/8),math.floor(high.y/8)+1):grid[(gx,gy)].append(idx)
 ev.to_mesh_clear()
print('BVH_READY',len(objs),'unique_meshes',len(cache),flush=True)
def candidates(x,y,r=0):
 ids=set()
 for gx in range(math.floor((x-r)/8),math.floor((x+r)/8)+1):
  for gy in range(math.floor((y-r)/8),math.floor((y+r)/8)+1):ids.update(grid.get((gx,gy),[]))
 return [objs[i] for i in ids if objs[i]['lo'].x-r<=x<=objs[i]['hi'].x+r and objs[i]['lo'].y-r<=y<=objs[i]['hi'].y+r]
def ray(it,origin,direction,distance):
 p=it['inv']@Vector(origin);v=it['inv'].to_3x3()@Vector(direction);factor=v.length;v.normalize();a,n,i,d=it['bvh'].ray_cast(p,v,distance*factor)
 return (it['matrix']@a,n,i,d/factor) if a is not None else None
def support(x,y,z,ignore=()):
 hits=[]
 for it in candidates(x,y):
  if not (it['support'] or it['compound_support']) or any(it['name'].startswith(q) for q in ignore):continue
  h=ray(it,(x,y,z+(.22 if it['compound_support'] else 5)),(0,0,-1),1 if it['compound_support'] else 15)
  if h:hits.append((h[0].z,it['name']))
 return max(hits,default=None)
def inside(it,p):
 # Exact parity on evaluated polygons, bounded by real world mesh AABB. An open canopy is not a closed volume.
 if not all(it['lo'][k]+1e-5<p[k]<it['hi'][k]-1e-5 for k in range(3)):return False
 q=it['inv']@p;direction=Vector((.91237,.32289,.25234)).normalized();count=0
 for _ in range(100):
  h,n,face,d=it['bvh'].ray_cast(q,direction,3000)
  if h is None:break
  count+=1;q=h+direction*.0001
 return count%2==1
def blocked(x,y,z,r=.30,height=1.803):
 found=[]
 for it in candidates(x,y,r):
  if not it['obstacle'] or it['hi'].z<=z+.20 or it['lo'].z>=z+height:continue
  bad=False
  for dz in [.22,.55,.95,1.4,height-.03]:
   p=Vector((x,y,z+dz));q=it['inv']@p;nearest=it['bvh'].find_nearest(q)
   if nearest[0] is not None:
    wp=it['matrix']@nearest[0]
    if wp.z>z+.20+1e-5 and (wp-p).length<r-1e-5:bad=True;break
   if inside(it,p):bad=True;break
  if not bad:
   for ang in range(0,360,45):
    xx=x+r*math.cos(math.radians(ang));yy=y+r*math.sin(math.radians(ang));h=ray(it,(xx,yy,z+.20),(0,0,1),height-.20)
    if h:bad=True;break
  if bad:found.append(it['name'])
 return found
# Counts based on actual local geometry, rather than role/manifest equality alone.
def bounds(n):
 it=byname.get(n)
 if it:return {'min':list(it['lo']),'max':list(it['hi']),'extent':list(it['hi']-it['lo'])}
report['world_terrain']=bounds('WORLD_Terrain_Continuous_800x800m')
# Topology of the actual terrain mesh: one connected vertex component and only outer boundary edges.
to=bpy.data.objects['WORLD_Terrain_Continuous_800x800m'];tm=to.data;parent=list(range(len(tm.vertices)))
def root(i):
 while parent[i]!=i:parent[i]=parent[parent[i]];i=parent[i]
 return i
for e in tm.edges:
 a,b=e.vertices;ra,rb=root(a),root(b)
 if ra!=rb:parent[rb]=ra
edges=collections.Counter()
for poly in tm.polygons:
 vv=list(poly.vertices)
 for a,b in zip(vv,vv[1:]+vv[:1]):edges[tuple(sorted((a,b)))]+=1
boundary=[e for e,n in edges.items() if n==1];internal=[]
for e in boundary:
 vv=[to.matrix_world@tm.vertices[i].co for i in e]
 on_outer=any(all(abs(v[k]-c)<.0001 for v in vv) for k in [0,1] for c in [-400,400])
 if not on_outer:internal.append([list(v) for v in vv])
report['terrain_topology']={'vertices':len(tm.vertices),'polygons':len(tm.polygons),'connected_components':len(set(root(i) for i in range(len(parent)))),'boundary_edges':len(boundary),'internal_boundary_edges':internal,'nonmanifold_edges_gt2':sum(n>2 for n in edges.values()),'zero_area_polygons':sum(p.area<1e-9 for p in tm.polygons)}
report['actual_scene_geometry_counts']={'SCN_'+str(i).zfill(2):sum(it['name'].startswith('SCN'+str(i).zfill(2)+'_') for it in objs) for i in range(1,13)}
report['actual_active_geometry_count']=len(objs);report['actual_scene_anchor_ids']=sorted(set(o.get('scene_id') for o in bpy.data.objects if o.get('scene_id')))
report['region_collections']=[c.name for c in bpy.context.scene.collection.children if c.name.startswith(('02_','03_','04_','05_','06_','07_'))]
report['route_checks']=[]
for rr in manifest['routes']:
 lateral=min(.75,max(0,rr['width_m']/2-.35))
 samples=[];issues=[];tot=0;supports=collections.Counter();obstruct=collections.Counter();no_ground=0;deviation=[]
 for seg,(p,q) in enumerate(zip(rr['points_m'],rr['points_m'][1:])):
  p=Vector(p);q=Vector(q);delta=q-p;L=Vector((delta.x,delta.y)).length;side=Vector((-delta.y,delta.x,0)).normalized();steps=max(1,math.ceil(L/.5))
  for i in range(steps+1):
   center=p+delta*(i/steps)
   for off in [-lateral,0,lateral]:
    t=center+side*off;tot+=1;su=support(t.x,t.y,t.z);bad=[]
    if not su or abs(su[0]-t.z)>.22:
     no_ground+=1;bad.append('support_grade_deviation');deviation.append(None if not su else round(su[0]-t.z,5))
    if su:supports[su[1]]+=1
    bs=blocked(t.x,t.y,t.z)
    for b in bs:obstruct[b]+=1
    if bs:bad+=bs
    if bad and len(issues)<150:issues.append({'point':[round(v,4) for v in t],'lateral_offset_m':off,'segment':seg,'support':su,'issues':bad})
 report['route_checks'].append({'id':rr['id'],'tested_lateral_offsets_m':[-lateral,0,lateral],'capsule_radius_m':.3,'samples':tot,'support_deviation_samples':no_ground,'max_abs_support_deviation':max([abs(x) for x in deviation if x is not None],default=0),'obstacles':dict(obstruct),'issues':issues,'support_objects':dict(supports)})
 print('ROUTE',rr['id'],tot,'ground',no_ground,'obstacles',dict(obstruct),flush=True)
# Independent service-link probes fill narrow responsibilities not expressed by named 12 routes.
report['additional_service_paths']=[]
for name,points in [('Gate_Cargo_Side',[(11,-86,0),(11,-69,0),(0,-69,0)]),('Academy_Double_Aisle',[(25,6,0),(34,6,0),(34,12,0)]),('Courtyard_Follower',[(24,-32,0),(35,-32,0),(35,-26,0)]),('Hall_West_Water_To_Ring',[(-19,302,12),(-10,302,12)]),('Hall_East_Water_To_Ring',[(29,302,12),(20,302,12)]),('SCN01_RescueRamp_Access',[(-60,-340.5,16),(-56.1,-340.5,16),(-56.1,-336,16.89),(-55.7,-335.45,16.89)])]:
 bad=[];count=0
 for aa,bb in zip(points,points[1:]):
  aa=Vector(aa);bb=Vector(bb);d=bb-aa;L=d.length;side=Vector((-d.y,d.x,0)).normalized();steps=max(1,math.ceil(L/.25))
  for j in range(steps+1):
   for off in ([0] if name=='SCN01_RescueRamp_Access' else [-.65,0,.65]):
    p=aa+d*j/steps+side*off;su=support(p.x,p.y,p.z);bs=blocked(p.x,p.y,p.z,r=.4 if name=='SCN01_RescueRamp_Access' else .3);count+=1
    if not su or abs(su[0]-p.z)>.22 or bs:bad.append({'p':list(p),'support':su,'obstacles':bs})
 report['additional_service_paths'].append({'name':name,'samples':count,'capsule_radius_m':.4 if name=='SCN01_RescueRamp_Access' else .3,'lateral_offsets_m':[0] if name=='SCN01_RescueRamp_Access' else [-.65,0,.65],'issues':bad})
# Actual gate plane bounds, floor support and aperture volume samples.
a=byname['SCN03_Gate_Pier-4'];b=byname['SCN03_Gate_Pier4'];l=byname['SCN03_Gate_Lintel'];floorhit=support(0,-80,0)
report['gate']={'net_width_m':b['lo'].x-a['hi'].x,'net_height_m':l['lo'].z-floorhit[0],'support':floorhit,'clear_center':not blocked(0,-80,0)}
# Training hall actual inner wall faces and over-ceiling rays. Exact corner post encroachment retained in report.
west=byname['SCN04_TrainingHall_west'];east=byname['SCN04_TrainingHall_east_Pier1'];south=byname['SCN04_TrainingHall_Wall-1'];north=byname['SCN04_TrainingHall_Wall1']
volume={'wall_face_rectangle_m':[east['lo'].x-west['hi'].x,north['lo'].y-south['hi'].y],'floor_bounds':bounds('SCN04_TrainingHall_ClearActivityFloor'),'corner_post_encroachments_m':{it['name']:[max(0,min(it['hi'].x,east['lo'].x)-max(it['lo'].x,west['hi'].x)),max(0,min(it['hi'].y,north['lo'].y)-max(it['lo'].y,south['hi'].y))] for it in objs if it['name'].startswith('SCN04_TrainingHall_TimberPost')},'ceiling_samples':[]}
roofit=byname['SCN04_TrainingHall_Roof']
for x,y in [(-46,-4),(-36,-4),(-26,-4),(-46,12),(-36,12),(-26,12),(-36,4)]:
 h=ray(roofit,(x,y,.2),(0,0,1),20);volume['ceiling_samples'].append({'xy':[x,y],'height_m':h[0].z if h else None})
report['training']=volume;report['courtyard_floor']=bounds('SCN07_Courtyard_18x16')
# Hall: a 4.20m radial annular strip r14.50..18.70 sampled at 1 degree and <=.20m radial pitch.
hall={'floor_bounds':bounds('SCN11_HallFloor_38m'),'ring_inner_radius_m':14.5,'ring_outer_radius_m':18.7,'tested_width_m':4.2,'samples':0,'support_failures':[],'obstacles':[],'actual_main_pillars':[x['name'] for x in objs if x['name'].startswith('SCN11_MainPillar')],'water_platforms':{x['name']:bounds(x['name']) for x in objs if x['name'].startswith('SCN11_WaterPlatform')}}
for ang in range(360):
 for j in range(22):
  rad=14.5+.2*j;x=5+rad*math.cos(math.radians(ang));y=302+rad*math.sin(math.radians(ang));su=support(x,y,12);bs=blocked(x,y,12,r=.01)
  hall['samples']+=1
  if not su or abs(su[0]-12)>.22:hall['support_failures'].append([round(x,4),round(y,4),su])
  if bs:hall['obstacles'].append([round(x,4),round(y,4),bs])
report['hall']=hall;print('HALL',hall['samples'],len(hall['support_failures']),len(hall['obstacles']),flush=True)
report['relay']={'tower':bounds('SCN12_PROP22_Tower'),'beacon':bounds('SCN12_CoreBeacon'),'platform':bounds('SCN12_CorePlatform'),'ground_rods':[x['name'] for x in objs if x['name'].startswith('SCN12_PROP14_GroundRod')],'low_covers':[x['name'] for x in objs if x['name'].startswith('SCN12_LowCover')],'invasion_route_objects':[x['name'] for x in objs if x['name'].startswith('ROUTE_Relay_Invasion')]}
report['waterworks']={'entry':bounds('SCN09_EntryObservation'),'target':bounds('SCN09_ControlPlatform'),'tower':bounds('SCN09_LowPressureTower'),'channels':[bounds('CHANNEL_'+str(i)+'_Bed') for i in [1,2,3]],'gate_like_objects':[x['name'] for x in objs if x['name'].startswith('SCN09') and any(a in x['name'].lower() for a in ['sidegate','sidedoor','side_gate','side_door','gateframe','doorframe','eastgate'])],'bridge_bank_objects':[x['name'] for x in objs if x['name'].startswith('SCN09') and x['obj'].get('role')=='walkable_bridge_bank']}
# Actual water visibility: top rays include terrain and buildings, never infer from water BBox presence.
def first_vertical(x,y,top):
 hits=[]
 for it in candidates(x,y):
  h=ray(it,(x,y,top),(0,0,-1),40)
  if h:hits.append((h[0].z,it['name']))
 return max(hits,default=None)
report['water_visibility']={}
for watername in ['SCN09_PoolWater']+['CHANNEL_'+str(i)+'_Water' for i in [1,2,3]]:
 it=byname[watername];lo,hi=it['lo'],it['hi'];points=[]
 # Interior grid avoids bank boundary ambiguity while still probing actual surface coverage.
 nx=max(3,math.ceil((hi.x-lo.x)/1.5));ny=max(3,math.ceil((hi.y-lo.y)/1.5))
 for ix in range(nx):
  for iy in range(ny):
   x=lo.x+(ix+.5)*(hi.x-lo.x)/nx;y=lo.y+(iy+.5)*(hi.y-lo.y)/ny
   wh=ray(it,(x,y,hi.z+1),(0,0,-1),2);th=ray(byname['WORLD_Terrain_Continuous_800x800m'],(x,y,hi.z+12),(0,0,-1),30);top=first_vertical(x,y,hi.z+12)
   points.append({'xy':[x,y],'water_z':wh[0].z if wh else None,'terrain_z':th[0].z if th else None,'first_hit':top,'visible':bool(top and top[1]==watername),'terrain_over_water':bool(th and wh and th[0].z>wh[0].z+.01)})
 report['water_visibility'][watername]={'samples':len(points),'water_first_hit_samples':sum(x['visible'] for x in points),'terrain_over_water_samples':sum(x['terrain_over_water'] for x in points),'points':points}
report['pool_boundaries']={it['name']:bounds(it['name']) for it in objs if it['name'].startswith(('SCN09_PoolBank','SCN09_PoolEndBank')) or it['name']=='SCN09_PoolBottom'}
report['abbey_arch_geometry']={it['name']:bounds(it['name']) for it in objs if it['name'].startswith('SCN10') and 'arch' in it['name'].lower()}
# Rescue interface and barrel support are sampled on the actual source/platform faces.
bar=byname['SCN01_PROP01_WaterBarrel'];bx=(bar['lo'].x+bar['hi'].x)/2;by=(bar['lo'].y+bar['hi'].y)/2;bz=bar['lo'].z;brx=(bar['hi'].x-bar['lo'].x)/2*.90;bry=(bar['hi'].y-bar['lo'].y)/2*.90
report['rescue_interface']={'barrel_actual_bounds':bounds(bar['name']),'barrel_base_samples':[],'actual_rescue_geometry':{it['name']:bounds(it['name']) for it in objs if it['name'].startswith('SCN01_Rescue')},'original_platform_surface_samples':[]}
for angle in [None]+[i*30 for i in range(12)]:
 x=bx if angle is None else bx+brx*math.cos(math.radians(angle));y=by if angle is None else by+bry*math.sin(math.radians(angle));su=support(x,y,bz)
 report['rescue_interface']['barrel_base_samples'].append({'xy':[x,y],'base_z':bz,'support':su,'vertical_gap_m':None if not su else bz-su[0]})
for x in [-55.5,-55.3,-55.2,-55.11]:
 for y in [-335.9,-335.5,-335.0]:
  h=ray(byname['SCN01_Rescue_Platform'],(x,y,18),(0,0,-1),3)
  report['rescue_interface']['original_platform_surface_samples'].append({'xy':[x,y],'platform_z':h[0].z if h else None})
# Provenance and instance identity are checked against actual datablocks and world singular scales.
inst=[o for o in bpy.data.objects if o.get('master')];badscale=[];badmesh=[]
for o in inst:
 sc=o.matrix_world.to_scale()
 if any(abs(v-1)>1e-5 for v in sc):badscale.append([o.name,list(sc)])
 master=bpy.data.objects.get(o['master'])
 if not master or o.data!=master.data:badmesh.append(o.name)
report['reuse']={'actual_instance_count':len(inst),'nonunit_world_scales':badscale,'not_shared_master_mesh':badmesh,'unexpected_fixture_collections':[c.name for c in bpy.data.collections if any(x in c.name.lower() for x in ['fixture','test_layout','testlayout','assembly_test','755'])]}
character=[o for c in bpy.data.collections if 'CHARACTER' in c.name for o in c.objects];verts=[o.matrix_world@v.co for o in character if o.type=='MESH' for v in o.data.vertices]
report['character']={'mesh_height_m':max(v.z for v in verts)-min(v.z for v in verts),'world_scales':{o.name:list(o.matrix_world.to_scale()) for o in character},'armatures':{o.name:len(o.data.bones) for o in character if o.type=='ARMATURE'}}
# UV/material binding validation, persisted after reopen.
uv_missing=[];empty=[];uv_uses=[]
for o in bpy.data.objects:
 if o.type!='MESH' or o.name not in active or o.name in exclude:continue
 for mat in o.data.materials:
  if not mat:empty.append(o.name);continue
  if mat.use_nodes:
   for nd in mat.node_tree.nodes:
    if nd.type=='UVMAP' and nd.uv_map:
     uv_uses.append([o.name,mat.name,nd.uv_map])
     if nd.uv_map not in o.data.uv_layers:uv_missing.append([o.name,mat.name,nd.uv_map,list(o.data.uv_layers.keys())])
report['materials']={'empty_material_slots':empty,'explicit_uv_missing':uv_missing,'explicit_uv_bindings_count':len(uv_uses),'material_count':len(bpy.data.materials)}
# Negative controls are in-memory probes only; no save or original modification.
report['negative_controls']={}
# Removing only the crossing deck must expose deep channel at its centre.
normal=support(270,-15,2);removed=support(270,-15,2,ignore=('SCN09_LowerBridge',))
report['negative_controls']['remove_bridge_support']={'point':[270,-15,2],'normal':normal,'removed':removed,'detected':bool(normal and abs(normal[0]-2)<.22 and (not removed or abs(removed[0]-2)>.22))}
# Verify obstruction query finds the real gate pier whereas aperture remains clear.
report['negative_controls']['gate_pier_vs_opening']={'opening_blocked':blocked(0,-80,0),'pier_blocked':blocked(4,-80,0),'detected':not blocked(0,-80,0) and bool(blocked(4,-80,0))}
report['negative_controls']['compound_floor_not_obstacle']={'point':[-66.6,-345,16.35],'support':support(-66.6,-345,16.35),'obstacles':blocked(-66.6,-345,16.35),'detected':not blocked(-66.6,-345,16.35)}
report['entry_080m_capsule']={'samples':0,'issues':[]}
for pp,qq in [((-60,-345,16),(-65,-345,16)),((-65,-345,16),(-66.6,-345,16.35)),((-66.6,-345,16.35),(-67.8,-345,16.35))]:
 pp=Vector(pp);qq=Vector(qq);n=math.ceil((qq-pp).length/.1)
 for i in range(n+1):
  p=pp+(qq-pp)*i/n;bs=blocked(p.x,p.y,p.z,r=.4);su=support(p.x,p.y,p.z);report['entry_080m_capsule']['samples']+=1
  if bs or not su or abs(su[0]-p.z)>.22:report['entry_080m_capsule']['issues'].append({'p':list(p),'obstacles':bs,'support':su})
# Bounded bridge cross-sections and requested sightlines are direct geometry probes.
report['bridge_clear_sections']=[]
for prefix,cx,cy,z in [('SCN08_MaintenanceBridge',-328,23,4),('SCN09_LowerBridge',270,-18,2),('SCN09_UpperBridge',270,22,2),('SCN10_WaterBridge',0,237,12)]:
 widths=[];fail=[]
 for yy in [cy+.1+i*.2 for i in range(30)]:
  allowed=[]
  for ix in range(81):
   xx=cx-2+ix*.05;su=support(xx,yy,z);ok=bool(su and abs(su[0]-z)<=.22 and not blocked(xx,yy,z,r=.01))
   allowed.append(ok)
  # Only the connected interval containing the centreline counts.
  if allowed[40]:
   lo=hi=40
   while lo and allowed[lo-1]:lo-=1
   while hi<80 and allowed[hi+1]:hi+=1
   widths.append((hi-lo)*.05)
  else:widths.append(0);fail.append(yy)
 report['bridge_clear_sections'].append({'bridge':prefix,'cross_sections':30,'lateral_pitch_m':.05,'min_sampled_clear_width_m':min(widths),'centre_fail_y':fail})
def sight(a,b):
 a=Vector(a);v=Vector(b)-a;length=v.length;v.normalize();nearest=None
 for it in objs:
  if it['obj'].get('role') in {'water_proxy','water_flow_proxy','fire_visual_proxy'}:continue
  h=ray(it,a,v,length-.05)
  if h and h[3]>.03 and (nearest is None or h[3]<nearest['distance_m']):nearest={'object':it['name'],'point':list(h[0]),'distance_m':h[3]}
 return nearest
report['sightlines']={'south_gate_to_plaza_hearth_first_hit':sight((0,-83,1.65),(0,0,1.6)),'SCN01_entry_to_south_gate':sight((-60,-354,17.65),(0,-80,5)),'SCN01_near_sign_to_south_gate':sight((-48,-292,14.65),(0,-80,5)),'SCN01_entry_to_gate_roof_samples':[{'target':[x,-80,7.5],'first_hit':sight((-60,-354,17.65),(x,-80,7.5))} for x in [-4,0,4]]}
# Water-occlusion negative control: inject a real BVH plane above one clear pool sample, query, remove.
before=first_vertical(305,20,14)
neg={'name':'NEGATIVE_CONTROL_POOL_OCCLUDER','bvh':BVHTree.FromPolygons([(304.5,19.5,2),(305.5,19.5,2),(305.5,20.5,2),(304.5,20.5,2)],[(0,1,2,3)]),'matrix':Matrix.Identity(4),'inv':Matrix.Identity(4),'lo':Vector((304.5,19.5,2)),'hi':Vector((305.5,20.5,2))}
ni=len(objs);objs.append(neg);cell=(math.floor(305/8),math.floor(20/8));grid[cell].append(ni)
after=first_vertical(305,20,14);grid[cell].remove(ni);objs.pop()
report['negative_controls']['cover_pool_with_bvh_plane']={'normal_first_hit':before,'covered_first_hit':after,'detected':bool(before and before[1]=='SCN09_PoolWater' and after and after[1]=='NEGATIVE_CONTROL_POOL_OCCLUDER'),'no_scene_or_file_mutation':True}
report['source_unchanged_during_review']=sha(SRC)==source_sha
json.dump(report,open(os.path.join(OUT,'independent_geometry_results.json'),'w'),ensure_ascii=False,indent=2)
print('REVIEW_DONE',source_sha,'unchanged',report['source_unchanged_during_review'],flush=True)
