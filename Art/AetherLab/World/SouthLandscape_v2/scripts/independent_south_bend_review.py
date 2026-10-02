"""Independent saved road support and ray localization for the Ridge_Reveal dark notch."""
import bpy,json,sys,argparse,hashlib,math,collections
from pathlib import Path
from mathutils import Vector,Matrix
from mathutils.bvhtree import BVHTree
ap=argparse.ArgumentParser();ap.add_argument('--source',required=True);ap.add_argument('--manifest',required=True);ap.add_argument('--receipt',required=True);ap.add_argument('--output',required=True);a=ap.parse_args(sys.argv[sys.argv.index('--')+1:])
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
source_sha=sha(a.source);bpy.ops.wm.open_mainfile(filepath=str(Path(a.source).resolve()));bpy.context.view_layer.update()
rr=next(r for r in json.load(open(a.manifest))['routes'] if r['id']=='C01_Mountain_Main');pts=[Vector(p) for p in rr['points_m']]
road=bpy.data.objects['ROUTE_C01_Mountain_Main'];terrain=bpy.data.objects['WORLD_Terrain_Continuous_800x800m']
def bvh(o):return BVHTree.FromPolygons([o.matrix_world@v.co for v in o.data.vertices],[tuple(p.vertices) for p in o.data.polygons],epsilon=1e-7)
rb,tb=bvh(road),bvh(terrain)
def vert_hit(b,x,y):
 q=b.ray_cast(Vector((x,y,100)),Vector((0,0,-1)),200)[0];return None if q is None else q.z
# Actual surface mesh geometric adjacency, welding only coincident positions.
co=[tuple(round(v,5) for v in road.matrix_world@p.co) for p in road.data.vertices];edgefaces=collections.defaultdict(list)
for p in road.data.polygons:
 ids=list(p.vertices)
 for aa,bb in zip(ids,ids[1:]+ids[:1]):edgefaces[tuple(sorted((co[aa],co[bb])))].append(p.index)
adj=collections.defaultdict(set)
for ff in edgefaces.values():
 for x in ff:adj[x].update(ff)
seen=set();components=0
for p in road.data.polygons:
 if p.index in seen:continue
 components+=1;todo=[p.index]
 while todo:
  i=todo.pop()
  if i in seen:continue
  seen.add(i);todo.extend(adj[i]-seen)
# Nominal strip probes. Boundaries excluded by 0.05m to avoid numerical edge ambiguity.
rows=[]
for bend in range(1,len(pts)-1):
 row={'bend_index':bend,'bend_point_m':list(pts[bend]),'along_each_side_m':12,'longitudinal_pitch_m':.20,'nominal_halfwidth_m':3,'tested_halfwidth_m':2.95,'lateral_pitch_m':.10,'samples':0,'inner_corridor_samples':0,'main_route_miss_samples':[],'grade_deviation_samples':[],'inner_corridor_failures':[],'max_abs_grade_delta_m':0}
 offsets=[-2.95+i*.1 for i in range(60)]+[-.75,0,.75]
 for seg in [bend-1,bend]:
  p,q=pts[seg],pts[seg+1];d=q-p;L=Vector((d.x,d.y)).length;u=d/L;side=Vector((-d.y,d.x,0)).normalized()
  for j in range(61):
   length=L-j*.2 if seg==bend-1 else j*.2
   if not 0<=length<=L:continue
   center=p+u*length
   for off in offsets:
    v=center+side*off;z=vert_hit(rb,v.x,v.y);tz=vert_hit(tb,v.x,v.y);row['samples']+=1;inner=abs(off)<=.750001
    if inner:row['inner_corridor_samples']+=1
    issue=None
    if z is None:issue='no_main_road_surface';row['main_route_miss_samples'].append({'point_m':list(v),'lateral_m':off,'terrain_z':tz,'segment':seg})
    elif abs(z-v.z)>.22:issue='main_road_grade_deviation';row['grade_deviation_samples'].append({'point_m':list(v),'lateral_m':off,'main_road_z':z,'terrain_z':tz,'segment':seg})
    if z is not None:row['max_abs_grade_delta_m']=max(row['max_abs_grade_delta_m'],abs(z-v.z))
    if inner and issue:row['inner_corridor_failures'].append({'point_m':list(v),'lateral_m':off,'issue':issue,'main_road_z':z,'terrain_z':tz})
 rows.append(row)
# Orthogonal rays in the saved fixed camera localize observed notch pixels against road/bed/terrain.
receipt=json.load(open(a.receipt));camrec=next(r for r in receipt['views'] if r['file']=='Ridge_Reveal.png');cm=Matrix(camrec['matrix_world']);W,H=camrec['resolution'];lens=camrec['lens_mm'];origin=cm.translation
candidates=[('ROUTE_C01_Mountain_Main',rb),('WORLD_Terrain_Continuous_800x800m',tb)]+[(o.name,bvh(o)) for o in bpy.data.objects if o.name.startswith('C01_Mountain_Main_BED_')]
def route_distance(x,y):
 best=None
 for i,(p,q) in enumerate(zip(pts,pts[1:])):
  d=q-p;t=max(0,min(1,((x-p.x)*d.x+(y-p.y)*d.y)/(d.x*d.x+d.y*d.y)));px,py=p.x+t*d.x,p.y+t*d.y;dist=math.hypot(x-px,y-py)
  if best is None or dist<best['distance_to_centerline_xy_m']:best={'segment':i,'distance_to_centerline_xy_m':dist,'authored_centerline_grade_z':p.z+t*d.z}
 return best
pixels=[]
for x,y in [(570,420),(590,425),(605,430),(620,433),(635,436),(645,434),(625,440),(645,440),(590,420),(615,420),(630,427),(655,434)]:
 direction=(cm.to_3x3()@Vector((((x+.5)/W-.5)*36/lens,(.5-(y+.5)/H)*36/lens*(H/W),-1))).normalized();hits=[]
 for n,b in candidates:
  hp,hn,fi,dist=b.ray_cast(origin,direction,1000)
  if hp is not None:hits.append((dist,n,hp,fi))
 hits.sort(key=lambda k:k[0]);hit=hits[0] if hits else None
 if hit:
  dist,n,hp,fi=hit;rz=vert_hit(rb,hp.x,hp.y);tz=vert_hit(tb,hp.x,hp.y)
  pixels.append({'pixel_xy':[x,y],'first_surface':n,'hit_point_m':list(hp),'surface_face_index':fi,'camera_distance_m':dist,'main_road_vertical_z':rz,'terrain_vertical_z':tz,'road_above_hit_m':None if rz is None else rz-hp.z,**route_distance(hp.x,hp.y)})
report={'source_sha256':source_sha,'method':'Reopened saved main-route mesh only BVH for support, independent world-space terrain BVH; no author geometry generated. Seven bends sampled over +/-12m, 0.20m longitudinal pitch and 0.10m cross offsets to +/-2.95m, plus +/-0.75m and center. Fixed camera rays target observed dark notch pixels.','main_route_faces':len(road.data.polygons),'main_route_position_welded_components':components,'position_weld_tolerance_decimal_places':5,'nonmanifold_edges_over_two_faces':sum(len(v)>2 for v in edgefaces.values()),'bend_checks':rows,'totals':{'probes':sum(r['samples'] for r in rows),'inner_corridor_probes':sum(r['inner_corridor_samples'] for r in rows),'main_route_miss_samples':sum(len(r['main_route_miss_samples']) for r in rows),'grade_deviation_samples':sum(len(r['grade_deviation_samples']) for r in rows),'inner_corridor_failures':sum(len(r['inner_corridor_failures']) for r in rows)},'notch_pixel_rays':pixels,'source_unchanged':source_sha==sha(a.source),'limits':['Discrete vertical support, not continuous swept-volume or UE collision/nav proof','Nominal-width probes exclude the exact outer 0.05m boundary','Camera localization includes only main route, its roadbed sides and terrain; vegetation/architecture are outside this targeted ray set']}
Path(a.output).write_text(json.dumps(report,ensure_ascii=False,indent=2));print(json.dumps({'totals':report['totals'],'components':components,'pixel_rays':pixels},ensure_ascii=False,indent=2),flush=True)
