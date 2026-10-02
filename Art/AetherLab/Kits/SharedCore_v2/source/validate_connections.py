"""Reopen-only actual-world-geometry audit. No constructor imports and no declaration coordinates used as proof."""
import bpy,os,sys,json,argparse,math,hashlib,bmesh
from mathutils import Vector
from mathutils.bvhtree import BVHTree
p=argparse.ArgumentParser();p.add_argument('--output-report',required=True);a=p.parse_args(sys.argv[sys.argv.index('--')+1:]);out=os.path.abspath(a.output_report)
s=bpy.context.scene;yard=bpy.data.collections['02_CONNECTION_FIXTURES__NOT_WORLD'];masters=bpy.data.collections['01_MASTER_MODULES__4m_Candidate'];obs=[o for o in yard.objects if o.type=='MESH'];checks=[];details={}
def check(name,passed,value):checks.append(dict(test=name,passed=bool(passed),measurement=value))
dep=bpy.context.evaluated_depsgraph_get()
def evaluated(o):
 e=o.evaluated_get(dep);me=e.to_mesh();v=[o.matrix_world@x.co for x in me.vertices];f=[list(x.vertices) for x in me.polygons];e.to_mesh_clear();return v,f
geo={o.name:evaluated(o) for o in obs}
def tree(objects):
 vs=[];fs=[]
 for o in objects:
  v,f=geo[o.name];off=len(vs);vs+=v;fs += [[i+off for i in q] for q in f]
 return BVHTree.FromPolygons(vs,fs,all_triangles=False) if vs else None

def bb(o):
 v=geo[o.name][0];return [min(p[i] for p in v) for i in range(3)],[max(p[i] for p in v) for i in range(3)]
def ray(t,x,y,z=10):
 h=t.ray_cast(Vector((x,y,z)),Vector((0,0,-1)),30) if t else (None,None,None,None)
 return h[0].z if h[0] is not None else None
roles={'road_support','road_terrain_transition','terrain','ramp_terrain_support','barrier_ground'}
support=[o for o in obs if o.get('role') in roles];stree=tree(support);ground_without_presentation=stree
check('scene metric unit scale',s.unit_settings.system=='METRIC' and abs(s.unit_settings.scale_length-1)<1e-8,[s.unit_settings.system,s.unit_settings.scale_length])
check('all instance mesh data shared with master',all(o.data in {m.data for m in masters.objects if m.type=='MESH'} for o in obs),{'instances':len(obs),'unique_meshes':len({o.data for o in obs})})
nonunit=[o.name for o in obs if max(abs(x-1) for x in o.scale)>1e-6];check('no per-instance scaling',not nonunit,nonunit)
newmasters=[m for m in masters.objects if m.get('role') and m.type=='MESH'];nonfinite=[];degenerate=[];badnorm=[];openmesh=[]
for m in newmasters:
 if any(not math.isfinite(c) for v in m.data.vertices for c in v.co):nonfinite.append(m.name)
 m.data.calc_loop_triangles()
 if any(t.area<1e-10 for t in m.data.loop_triangles):degenerate.append(m.name)
 bm=bmesh.new();bm.from_mesh(m.data)
 if any(not e.is_manifold for e in bm.edges):openmesh.append(m.name)
 if bm.calc_volume(signed=True)<-1e-8:badnorm.append(m.name)
 bm.free()
check('new connection meshes finite and nondegenerate',not(nonfinite or degenerate),{'nonfinite':nonfinite,'degenerate':degenerate})
check('new meshes closed manifold edge topology',not openmesh,openmesh)
check('new meshes outward signed volume',not badnorm,badnorm)
missinguv=[m.name for m in masters.objects if m.type=='MESH' and not m.data.uv_layers.get('SurfaceUV')];check('master SurfaceUV layer present',not missinguv,missinguv)
missingmaps=[]
for m in masters.objects:
 if m.type!='MESH':continue
 for mat in m.data.materials:
  if mat and mat.use_nodes:
   for n in mat.node_tree.nodes:
    if n.type=='UVMAP' and n.uv_map and n.uv_map not in m.data.uv_layers:missingmaps.append([m.name,mat.name,n.uv_map])
check('explicit material UV references resolve',not missingmaps,missingmaps)
imgs=[{'name':i.name,'packed':bool(i.packed_file)} for i in bpy.data.images if i.source=='FILE' and i.users];check('referenced images packed',all(i['packed'] for i in imgs),imgs)
# Ground coverage sampled across every actual tile/patch footprint, not a fixture centerline or presentation floor.
for fix in sorted({o.get('fixture') for o in support}):
 selected=[o for o in support if o.get('fixture')==fix];t=tree(selected);miss=[];n=0
 for o in selected:
  lo,hi=bb(o)
  # Axis-aligned footprints arise from 90-degree instances; polygon/BVH is still the evidence.
  nx=max(2,math.ceil((hi[0]-lo[0])/.20));ny=max(2,math.ceil((hi[1]-lo[1])/.20))
  for i in range(nx+1):
   for j in range(ny+1):
    x=lo[0]+.0002+(hi[0]-lo[0]-.0004)*i/nx;y=lo[1]+.0002+(hi[1]-lo[1]-.0004)*j/ny;n+=1
    if ray(t,x,y) is None:miss.append([o.name,x,y])
 check(f'{fix} full support footprint sampling',not miss,{'sample_count':n,'pitch_m':.20,'misses':miss[:20],'excluded':'presentation platform, labels, character'})
# Compare both sides of real shared road-base edges across their full width, including all road loop/T/X junctions.
roads=[o for o in obs if o.get('role')=='road_support' and o.get('fixture') in ['ROAD_LOOP_T','ROAD_X']];seams=[]
for i,o in enumerate(roads):
 lo,hi=bb(o)
 for q in roads[i+1:]:
  l,h=bb(q)
  for axis in [0,1]:
   cross=1-axis
   if abs(hi[axis]-l[axis])<1e-5 or abs(h[axis]-lo[axis])<1e-5:
    edge=hi[axis] if abs(hi[axis]-l[axis])<1e-5 else lo[axis];a0=max(lo[cross],l[cross]);a1=min(hi[cross],h[cross])
    if a1-a0<.01:continue
    gaps=[]
    for k in range(41):
     v=a0+.001+(a1-a0-.002)*k/40;xy=[0,0];xy[cross]=v;xy[axis]=edge-.0005;z0=ray(stree,*xy);xy[axis]=edge+.0005;z1=ray(stree,*xy)
     gaps.append(None if z0 is None or z1 is None else abs(z1-z0))
    seams.append({'objects':[o.name,q.name],'axis':axis,'edge_world':edge,'samples':len(gaps),'max_height_gap_m':max(g for g in gaps if g is not None) if any(g is not None for g in gaps) else None,'misses':sum(g is None for g in gaps)})
check('all adjoining flat road seams full-width',all(q['misses']==0 and q['max_height_gap_m']<.00002 for q in seams),{'seams':len(seams),'records':seams,'tolerance_m':.00002})
# Every paved road's exposed edge samples transition support either side; nearest geometric ray hit proves no slit.
edgechecks=[]
for o in roads:
 lo,hi=bb(o)
 for axis in [0,1]:
  other=1-axis
  for edge in [lo[axis],hi[axis]]:
   for k in range(21):
    xy=[0,0];xy[other]=lo[other]+.002+(hi[other]-lo[other]-.004)*k/20;xy[axis]=edge-.001;z0=ray(stree,*xy);xy[axis]=edge+.001;z1=ray(stree,*xy)
    edgechecks.append(None if z0 is None or z1 is None else abs(z1-z0))
check('road edge to shoulder/neighbor full-width continuity',all(v is not None and v<.001 for v in edgechecks),{'samples':len(edgechecks),'missing':sum(v is None for v in edgechecks),'max_height_gap_m':max(v for v in edgechecks if v is not None),'tolerance_m':.001})
# Each reusable transition piece has expected world vertices and no missed top surface, checked across all actual shared patch edges.
trans=[o for o in support if o.get('role')=='road_terrain_transition'];tseams=[]
for i,o in enumerate(trans):
 lo,hi=bb(o)
 for q in trans[i+1:]:
  l,h=bb(q)
  for axis in [0,1]:
   cross=1-axis
   if (abs(hi[axis]-l[axis])<1e-5 or abs(h[axis]-lo[axis])<1e-5) and min(hi[cross],h[cross])-max(lo[cross],l[cross])>.01:
    edge=hi[axis] if abs(hi[axis]-l[axis])<1e-5 else lo[axis];v=(max(lo[cross],l[cross])+min(hi[cross],h[cross]))/2;xy=[0,0];xy[cross]=v;xy[axis]=edge-.0005;z0=ray(stree,*xy);xy[axis]=edge+.0005;z1=ray(stree,*xy)
    tseams.append(None if z0 is None or z1 is None else abs(z0-z1))
check('convex concave shoulder patch seam heights',all(v is not None and v<.001 for v in tseams),{'seams':len(tseams),'missing':sum(v is None for v in tseams),'max_gap_m':max(v for v in tseams if v is not None),'tolerance_m':.001})
# Upright pillar / post bases must enter real support. Sample inset vertices around each footprint, excluding other barriers.
nodes=[o for o in obs if o.get('role')=='joint_support'];floating=[];bases=[]
for o in nodes:
 lo,hi=bb(o);gaps=[]
 for x in [lo[0]+.005,(lo[0]+hi[0])/2,hi[0]-.005]:
  for y in [lo[1]+.005,(lo[1]+hi[1])/2,hi[1]-.005]:
   z=ray(stree,x,y);gaps.append(None if z is None else lo[2]-z)
 if any(v is None or v>.003 for v in gaps):floating.append({'object':o.name,'base_world_m':lo[2],'gaps_m':gaps})
 bases.extend(gaps)
check('all joint supports touch or embed actual ground',not floating,{'supports':len(nodes),'samples':len(bases),'max_positive_gap_m':max(v for v in bases if v is not None),'failures':floating,'tolerance_m':.003})
# Segment ends are measured from the actual mesh projected onto the object's longitudinal axis.
segments=[o for o in obs if o.get('role') in ['wall','fence']];links=[];jointerrors=[]
for o in segments:
 v,f=geo[o.name];axis=(o.matrix_world.to_3x3()@Vector((0,1,0))).normalized();origin=o.matrix_world.translation;projs=[(p-origin).dot(axis) for p in v];ends=[]
 for dist in [min(projs),max(projs)]:
  pt=origin+axis*dist;candidates=[n for n in nodes if n.get('fixture')==o.get('fixture') and (Vector((n.location.x,n.location.y,0))-Vector((pt.x,pt.y,0))).length<.6]
  if len(candidates)!=1:jointerrors.append([o.name,tuple(pt),[n.name for n in candidates]]);continue
  n=candidates[0];nt=tree([n]);ot=tree([o]);nl,nh=bb(n)
  # Read two distinct beam center heights from the actual authored rail bounds; wall body at two observed bands.
  for zlocal in ([.53,.93] if o.get('role')=='fence' else [.25,.55]):
   h=origin.z+(.25*dist if 'Rise_' in o.get('asset_id','') else 0)+zlocal
   point=Vector((pt.x,pt.y,h));hit=nt.find_nearest(point);inside_xy=nl[0]-.01<=pt.x<=nh[0]+.01 and nl[1]-.01<=pt.y<=nh[1]+.01;inside_z=nl[2]<=h<=nh[2]
   if not(hit and hit[0] is not None and inside_xy and inside_z):jointerrors.append([o.name,n.name,h,'endpoint not within support geometry bounds'])
  ends.append(n.name)
 if len(ends)==2:links.append({'segment':o.name,'fixture':o.get('fixture'),'supports':ends})
check('all wall/fence measured ends have one shared support',not jointerrors,{'segments':len(segments),'links':links,'errors':jointerrors})
for fix in ['WALL_LOOP','FENCE_LOOP']:
 graph={}
 for q in links:
  if q['fixture']==fix:
   a0,b0=q['supports'];graph.setdefault(a0,[]).append(b0);graph.setdefault(b0,[]).append(a0)
 seen=set();todo=list(graph)[:1]
 while todo:
  n=todo.pop()
  if n in seen:continue
  seen.add(n);todo+=graph[n]
 check(f'{fix} actual connected closed cycle',len(graph)==8 and len(seen)==8 and all(len(v)==2 for v in graph.values()),{'nodes':len(graph),'connected_nodes':len(seen),'degrees':{k:len(v) for k,v in graph.items()}})
# Dense ramp cross-section rays at all X positions across road and banks, seam height at both ends.
high=[o for o in obs if o.get('fixture')=='HEIGHT_MIX'];ht=tree([o for o in high if o.get('role') in roles]);rb=next(o for o in high if o.name=='HEIGHT_RISE_Bank');lo,hi=bb(rb);rise_miss=[];rise_seams=[]
for edge in [lo[1],hi[1]]:
 for k in range(121):
  x=lo[0]+.002+(hi[0]-lo[0]-.004)*k/120;z0=ray(ht,x,edge-.0005);z1=ray(ht,x,edge+.0005)
  if z0 is None or z1 is None:rise_miss.append([x,edge])
  else:rise_seams.append(abs(z0-z1))
check('ramp low/high entire bank cross-section continuity',not rise_miss and max(rise_seams)<.0002,{'samples':len(rise_seams),'misses':rise_miss,'max_gap_m':max(rise_seams),'tolerance_m':.0002})
# Capsule corridor clearance is a static sampled geometric envelope, NOT runtime movement/collision certification.
obstacles=tree([o for o in high if o.get('role') in ['wall','fence','joint_support','wall_support']]);floorT=tree([o for o in high if o.get('role') in roles|{'road_surface'}]);paves=[o for o in high if o.get('role')=='road_surface'];bbv=[bb(o) for o in paves];xmin=min(b[0][0] for b in bbv);xmax=max(b[1][0] for b in bbv);ymin=min(b[0][1] for b in bbv);ymax=max(b[1][1] for b in bbv);xc=(xmin+xmax)/2
for height in [1.65,1.8027965174987912]:
 misses=[];near=[];ground=[]
 for k in range(121):
  y=ymin+.45+(ymax-ymin-.9)*k/120;zs=[ray(floorT,xc+dx,y+dy) for dx,dy in [(0,0),(-.4,0),(.4,0),(0,-.4),(0,.4)]]
  if any(z is None for z in zs):misses.append(y);continue
  foot=max(zs);ground.append(foot)
  for j in range(11):
   z=foot+.4+(height-.8)*j/10;hit=obstacles.find_nearest(Vector((xc,y,z)))
   if hit and hit[0] is not None:near.append(hit[3])
 check(f'height route sampled 0.8m diameter capsule h={height:.4f}',not misses and min(near)>.4,{'positions':121,'axis_samples_per_position':11,'ground_samples_per_position':5,'min_obstacle_distance_m':min(near),'radius_m':.4,'unsupported':misses,'geometry_only':True})
# Character source remains unchanged in size; separate preservation script checks weights/topology/rest exactly.
ch=bpy.data.collections['05_EXISTING_CHARACTER__UNSCALED_65_BONES'];vs=[o.matrix_world@v.co for o in ch.objects if o.type=='MESH' for v in o.data.vertices];height=max(v.z for v in vs)-min(v.z for v in vs);arm=[o for o in ch.objects if o.type=='ARMATURE'];check('unscaled 65-bone character height retained',len(arm)==1 and len(arm[0].data.bones)==65 and abs(height-1.8027965174987912)<1e-6,{'height_m':height,'design_height_m':1.65,'bones':len(arm[0].data.bones)})
report={'version':2,'blender_version':bpy.app.version_string,'file':os.path.basename(bpy.data.filepath),'file_sha256':hashlib.sha256(open(bpy.data.filepath,'rb').read()).hexdigest(),'date_utc':'2026-10-01','method':'Fresh Blender process, evaluated world-space vertices/polygons and BVH rays; presentation platform excluded. Samples prove only their recorded resolution; not a mathematical all-resolution guarantee.','checks':checks,'all_checks_passed':all(c['passed'] for c in checks),'master_count':len([o for o in masters.objects if o.type=='MESH']),'instance_count':len(obs),'limitations':['Static geometry only; no engine navigation, collision, animation, multiplayer, LOD or GPU test','Surface stone gaps are intentional soil-filled joints; final material/style/UV quality not accepted','Stairs, bridge-bank, rock transitions and other scene kits remain open','No full-world assembly performed'],'fixture_bounds':{fix:{'min':[min(bb(o)[0][i] for o in obs if o.get('fixture')==fix) for i in range(3)],'max':[max(bb(o)[1][i] for o in obs if o.get('fixture')==fix) for i in range(3)]} for fix in sorted({o.get('fixture') for o in obs})}}
os.makedirs(os.path.dirname(out),exist_ok=True);json.dump(report,open(out,'w'),ensure_ascii=False,indent=2);print(json.dumps({'checks':len(checks),'passed':sum(c['passed'] for c in checks),'failed':[c for c in checks if not c['passed']]},ensure_ascii=False))
if not report['all_checks_passed']:raise SystemExit(1)
