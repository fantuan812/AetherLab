"""Read independently reopened v2 geometry; only the JSON report is written.
Usage: blender -t 2 -b source/AetherLab_CoreKit_Interface_v2.blend --python source/independent_geometry_review.py -- --output-report docs/Independent_Geometry_Review.json
The optional negative control changes one object in memory and restores it. Never saves the blend.
"""
import bpy, math, json, hashlib, os, bmesh, argparse, sys
from collections import Counter,defaultdict
from mathutils import Vector
from mathutils.bvhtree import BVHTree
parser=argparse.ArgumentParser(description='Independent reopened SharedCore v2 geometry and negative-control checks')
parser.add_argument('--output-report',default='Independent_Geometry_Review.json',help='JSON output; relative paths are resolved from the current working directory')
args=parser.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
REPORT=os.path.abspath(args.output_report)
os.makedirs(os.path.dirname(REPORT),exist_ok=True)
Y=[o for o in bpy.data.collections['02_CONNECTION_FIXTURES__NOT_WORLD'].objects if o.type=='MESH']
M=list(bpy.data.collections['01_MASTER_MODULES__4m_Candidate'].objects)
dg=bpy.context.evaluated_depsgraph_get();G={};tests=[]
def check(name,n,bad,limit,extra=None):
 r=dict(test=name,samples=n,failures=len(bad),failure_examples=bad[:20],tolerance=limit,passed=not bad)
 if extra:r.update(extra)
 tests.append(r)
def geom(o):
 ev=o.evaluated_get(dg);me=ev.to_mesh();v=[o.matrix_world@p.co for p in me.vertices];f=[list(p.vertices) for p in me.polygons];ev.to_mesh_clear();return v,f
for o in Y:G[o.name]=geom(o)
def tree(obs):
 v=[];f=[]
 for o in obs:
  ov,of=G[o.name];k=len(v);v+=ov;f += [[k+i for i in p] for p in of]
 return BVHTree.FromPolygons(v,f,all_triangles=False,epsilon=0.0)
def hit(b,x,y,z=10):
 q=b.ray_cast(Vector((x,y,z)),Vector((0,0,-1)),30)
 return q[0].z if q[0] is not None else None
SUP={'road_support','road_terrain_transition','terrain','ramp_terrain_support','barrier_ground'}
def group(fix,roles):return [o for o in Y if o.get('fixture')==fix and o.get('role') in roles]
for fix,cells in [('ROAD_LOOP_T',[(i,j) for i in range(4) for j in range(4) if i in [0,3] or j in [0,3]]+[(4,1)]),('ROAD_X',[(7,1),(6,1),(8,1),(7,0),(7,2)])]:
 rects=[(4*i-2,4*i+2,4*j,4*j+4) for i,j in cells]
 b=tree(group(fix,SUP));allb=tree(group(fix,SUP|{'road_surface'}));n=0;bad=[];surf=[]
 # Coordinate grid independent of object's sockets or copied custom properties.
 for ix in range(int(min(r[0] for r in rects)*10)-10,int(max(r[1] for r in rects)*10)+11):
  for iy in range(int(min(r[2] for r in rects)*10)-10,int(max(r[3] for r in rects)*10)+11):
   x=ix/10+0.000173;y=iy/10+0.000219
   dist=min(max(max(a-x,0,x-c),max(d-y,0,y-e)) for a,c,d,e in rects)
   if dist>=1:continue
   exp=-.035-.315*dist;z=hit(b,x,y);n+=1
   if z is None or abs(z-exp)>.0002:bad.append([x,y,z,exp])
   zz=hit(allb,x,y)
   if zz is None or zz<exp-.0002:surf.append([x,y,zz,exp])
 check(fix+' footprint and shoulders independent 0.1m raster',n,bad,.0002)
 check(fix+' visible top never falls below continuous support',n,surf,.0002)
 # Zoom test either side of every internal 1m grid seam, not exactly on a tessellation edge.
 n=0;bad=[]
 for ix in range(int(min(r[0] for r in rects))-1,int(max(r[1] for r in rects))+2):
  for iy in range(int(min(r[2] for r in rects))-1,int(max(r[3] for r in rects))+2):
   for t in [.07,.19,.37,.53,.71,.89]:
    for orient in [0,1]:
     p=[ix,iy+t] if orient==0 else [ix+t,iy]
     a=p.copy();c=p.copy();a[orient]-=.0005;c[orient]+=.0005
     da=min(max(max(r[0]-a[0],0,a[0]-r[1]),max(r[2]-a[1],0,a[1]-r[3])) for r in rects)
     dc=min(max(max(r[0]-c[0],0,c[0]-r[1]),max(r[2]-c[1],0,c[1]-r[3])) for r in rects)
     if da>=1 or dc>=1:continue
     za=hit(b,*a);zc=hit(b,*c);n+=1
     if za is None or zc is None or abs(za-zc)>.0007:bad.append([a,c,za,zc])
 check(fix+' shoulder and tile seam strips',n,bad,.0007)
# Earth end is checked with its neighbor X fixture, only production support surfaces.
earth=tree(group('ROAD_X',SUP)+group('ROAD_EARTH_END',SUP));n=0;bad=[]
for ix in range(1,40):
 x=26+ix*.1
 for y in [-1.001,-1.0001,-.9999,-.999,-2.999,-2.0001,-1.9999]:
  z=hit(earth,x,y);exp=-.35 if y<=-1 else -.035-.315*(-y);n+=1
  if z is None or abs(z-exp)>.0002:bad.append([x,y,z,exp])
check('Road X shoulder to terrain end cross-kit seam',n,bad,.0002)
# Entire height support including slope sides, with banks not presentation floor.
bank=tree(group('HEIGHT_MIX',{'road_support','road_terrain_transition','ramp_terrain_support'}));n=0;bad=[]
for ix in range(1,120):
 x=23+ix*.05
 for iy in range(1,240):
  y=-16+iy*.05;roadz=-.035+min(1,max(0,(y+12)/4));d=max(0,abs(x-26)-2);exp=roadz+(-.35-roadz)*d;z=hit(bank,x,y);n+=1
  if z is None or (abs(x-26)<2 and abs(z-exp)>.0003) or z<-.3503 or z>roadz+.0003:bad.append([x,y,z,exp])
check('Height fixture central grade exact, side-bank no holes or overshoot',n,bad,.0003)
n=0;bad=[]
for seam in [-12,-8]:
 for ix in range(1,120):
  x=23+ix*.05;za=hit(bank,x,seam-.0005);zc=hit(bank,x,seam+.0005);n+=1
  if za is None or zc is None or abs(za-zc)>.0005:bad.append([x,seam,za,zc])
check('Height ramp lower/upper seam full-width',n,bad,.0005)
# Actual wall foundation/ground contacts at many positions and stone base support.
allground=tree([o for o in Y if o.get('role') in SUP]);groundtrees={f:tree(group(f,SUP)) for f in {o.get('fixture') for o in Y}};n=0;bad=[]
for o in Y:
 if o.get('role')!='wall_support':continue
 # Sample underside local coordinates at three lateral strips, 41 stations.
 for u in [-.3,0,.3]:
  for k in range(41):
   v=k*.1;p=o.matrix_world@Vector((u,v,0));rise=.25*v if 'Rise' in o.get('master','') else 0;zbase=o.location.z-.155+rise;z=hit(groundtrees[o.get('fixture')],p.x,p.y);n+=1
   if z is None or zbase-z>.001:bad.append([o.name,p.x,p.y,zbase,z])
check('Wall footing bases intersect production ground/bank',n,bad,.001)
# Upright support bottom contact; widened piers may overhang slopes deliberately but center must be supported.
n=0;bad=[];postgrounds=[]
for o in Y:
 if o.get('role')!='joint_support':continue
 vs=G[o.name][0];lo=min(p.z for p in vs);p=o.matrix_world.translation;z=hit(groundtrees[o.get('fixture')],p.x,p.y);n+=1
 postgrounds.append([o.name,lo,z,None if z is None else z-lo])
 if z is None or lo-z>.001:bad.append([o.name,lo,z])
check('44 upright pier/post centers embed into support',n,bad,.001,{'contacts':postgrounds})
# Test real lower support vertices and lower perimeter edge midpoints, including the exposed outer half at route ends.
n=0;bad=[];basecontacts=[]
for o in Y:
 if o.get('role')!='joint_support':continue
 vs,fs=G[o.name];lo=min(p.z for p in vs);inds={i for i,p in enumerate(vs) if p.z<lo+.0001};points=[vs[i] for i in inds];edges=set()
 for f in fs:
  for i in range(len(f)):
   a,b=f[i],f[(i+1)%len(f)]
   if a in inds and b in inds:edges.add(tuple(sorted((a,b))))
 points += [(vs[a]+vs[b])/2 for a,b in edges]
 for p in points:
  z=hit(groundtrees[o.get('fixture')],p.x,p.y);n+=1
  if z is None or z<p.z-.001:bad.append([o.name,list(p),z])
check('Actual full bottom footprints of upright supports have backing',n,bad,.001)
# Support duplicates use actual base centroids, not asset-id-specific counts.
locs=defaultdict(list)
for o in Y:
 if o.get('role')=='joint_support':locs[tuple(round(v,5) for v in o.matrix_world.translation)].append(o.name)
bad=[[list(k),v] for k,v in locs.items() if len(v)>1];check('No coincident duplicate upright supports',len(locs),bad,1e-5)
# Barrier endpoints: real endpoint mesh vertices must enter a single shared support mesh.
def component_trees(o):
 vs,fs=G[o.name];adj={i:set() for i in range(len(vs))}
 for f in fs:
  for i in f:adj[i].update(f)
 unseen=set(adj);out=[]
 while unseen:
  seed=unseen.pop();component={seed};todo=[seed]
  while todo:
   k=todo.pop();more=adj[k]&unseen;unseen-=more;component|=more;todo.extend(more)
  faces=[f for f in fs if f[0] in component];out.append(BVHTree.FromPolygons(vs,faces,all_triangles=False))
 return out
jointtrees={o.name:component_trees(o) for o in Y if o.get('role')=='joint_support'}
n=0;bad=[];endpoint_rows=[]
for o in Y:
 if o.get('role') not in {'wall','fence'}:continue
 for edge in [0,4]:
  pp=o.matrix_world@Vector((0,edge,.25*edge if 'Rise' in o.get('master','') else 0))
  js=[j for j in Y if j.name in jointtrees and j.get('fixture')==o.get('fixture') and (j.matrix_world.translation.xy-pp.xy).length<.03]
  if len(js)!=1:bad.append([o.name,edge,'support_count',len(js)]);continue
  jt=jointtrees[js[0].name];local=[v.co for v in o.data.vertices if abs(v.co.y-edge)<.04];outs=[]
  for p in local:
   wp=o.matrix_world@p;qs=[tt.find_nearest(wp) for tt in jt];n+=1
   # Closed support meshes have outward normals; negative signed nearest distance is inside.
   if not any(q[0] is not None and (wp-q[0]).dot(q[1])<=.002 for q in qs):outs.append(list(wp))
  if outs:bad.append([o.name,edge,js[0].name,'outside_endpoint_vertices',len(outs),outs[:3]])
  endpoint_rows.append([o.name,edge,js[0].name,len(local),len(outs)])
check('Barrier actual endpoint mesh vertices embed in one support',n,bad,.002,{'endpoints':endpoint_rows})
# Mesh and material state, based on reopened evaluated mesh and exact datablock identity.
mm={o.data for o in M if o.type=='MESH'};bad=[]
for o in Y:
 if o.type!='MESH':continue
 if o.data not in mm or any(abs(v-1)>1e-6 for v in o.scale) or not all(math.isfinite(c) for v in G[o.name][0] for c in v):bad.append(o.name)
check('Fixture geometry linked, unit-scale and finite',len(Y),bad,1e-6)
meshrows=[]
for o in M:
 if o.type!='MESH':continue
 bm=bmesh.new();bm.from_mesh(o.data);bm.normal_update();vol=bm.calc_volume(signed=True);nonman=sum(not e.is_manifold for e in bm.edges);inconsistent=sum(e.is_manifold and not e.is_contiguous for e in bm.edges);deg=sum(f.calc_area()<1e-10 for f in bm.faces);bm.free()
 o.data.calc_loop_triangles();deg_tri=sum(t.area<1e-10 for t in o.data.loop_triangles)
 uv=o.data.uv_layers.get('SurfaceUV');zero=0
 if uv:
  for f in o.data.polygons:
   pts=[uv.data[i].uv for i in f.loop_indices];ar=abs(sum(pts[i].x*pts[(i+1)%len(pts)].y-pts[(i+1)%len(pts)].x*pts[i].y for i in range(len(pts)))/2)
   if ar<1e-10:zero+=1
 meshrows.append(dict(master=o.name,vertices=len(o.data.vertices),polygons=len(o.data.polygons),signed_volume=vol,nonmanifold_edges=nonman,inconsistent_face_winding_edges=inconsistent,degenerate_faces=deg,degenerate_polygon_count=deg,degenerate_triangle_count=deg_tri,zero_area_material_uv_polygons=zero,materials=[m.name for m in o.data.materials]))
active={o.get('master') for o in Y};active_rows=[r for r in meshrows if r['master'] in active]
check('Active master meshes closed, positive volume, non-degenerate',len(active_rows),[x for x in active_rows if x['nonmanifold_edges'] or x['inconsistent_face_winding_edges'] or x['signed_volume']<=0 or x['degenerate_polygon_count'] or x['degenerate_triangle_count']],1e-10)
check('Active master SurfaceUV has no zero-area polygon projection',len(active_rows),[x for x in active_rows if x['zero_area_material_uv_polygons']],1e-10)
# Metadata is separate from geometry acceptance: reject stale local socket copies and mismatched master Empty locations.
masters_by_name={o.name:o for o in M if o.type=='MESH'};socket_keys=set();contracts={};metadata_bad=[];metadata_count=0
for name,o in masters_by_name.items():
 try:contract=json.loads(o.get('connectors_json','{}'))
 except Exception:contract={};metadata_bad.append([name,'invalid connectors_json'])
 if not contract:metadata_bad.append([name,'missing explicit connector contract'])
 contracts[name]=contract;socket_keys.update(contract)
 for key,xyz in contract.items():
  metadata_count+=1;marker=bpy.data.objects.get(name+'__'+key)
  if marker is None or marker.parent!=o or (marker.location-Vector(xyz)).length>1e-6:metadata_bad.append([name,key,'marker/local coordinate mismatch'])
  if key not in o or (Vector(o[key])-Vector(xyz)).length>1e-6:metadata_bad.append([name,key,'master property mismatch'])
for o in Y:
 name=o.get('master');contract=contracts.get(name,{})
 for key in socket_keys:
  if key not in o:continue
  metadata_count+=1
  if key not in contract or (Vector(o[key])-Vector(contract[key])).length>1e-6:metadata_bad.append([o.name,key,list(o[key]),contract.get(key)])
check('Master socket Empty coordinates and instance properties agree',metadata_count,metadata_bad,1e-6)
# Cross-section passage width measured by actual obstacle rays at 96 heights/stations.
obs=tree(group('HEIGHT_MIX',{'wall','wall_support','fence','joint_support'}));widths=[]
for y in [-15.8,-14,-12.05,-11.95,-10,-8.05,-7.95,-6,-4.2]:
 ground=-.035+min(1,max(0,(y+12)/4))
 for dz in [.1,.3,.6,.9,1.2,1.6,1.8028]:
  z=ground+dz;left=obs.ray_cast(Vector((26,y,z)),Vector((-1,0,0)),4);right=obs.ray_cast(Vector((26,y,z)),Vector((1,0,0)),4)
  xl=left[0].x if left[0] is not None else 24;xr=right[0].x if right[0] is not None else 28;widths.append([y,z,xr-xl,xl,xr])
check('Height fixture measured central clear span >=2.58m',len(widths),[v for v in widths if v[2]<2.58-1e-3],.001,{'minimum_measured_m':min(v[2] for v in widths),'measurements':widths})
# Static capsule geometry envelope. 0.8m is a review sample, not a gameplay specification.
# Find nearest obstacle surfaces along vertical capsule axes; 0.01m safety margin covers half the axis sample interval.
caprows=[]
for height in [1.65,1.8028]:
 bad=[];n=0;minimum=100;worst=None
 for iy in range(113):
  y=-15.6+iy*.1;floor=-.035+min(1,max(0,(y+12)/4));bottom=floor+.4;top=floor+height-.4;count=math.ceil((top-bottom)/.02)
  for k in range(count+1):
   p=Vector((26,y,bottom+(top-bottom)*k/count));q=obs.find_nearest(p);distance=q[3] if q[0] is not None else 100;n+=1
   if distance<minimum:minimum=distance;worst=list(p)
   if distance<.41:bad.append([list(p),distance])
 row={'height_m':height,'diameter_m':.8,'axis_interval_max_m':.02,'route_station_interval_m':.1,'minimum_obstacle_distance_from_axis_m':minimum,'conservative_capsule_clearance_m':minimum-.41,'worst_axis_point':worst,'not_runtime_collision':True}
 caprows.append(row);check('Static central capsule envelope height '+str(height),n,bad,.01,row)
# Negative control uses the identical ground/support criterion with a real object transform changed only in memory.
o=bpy.data.objects['HEIGHT_fence_Node0'];original_location=o.location.copy();original_geo=G[o.name];o.location.z+=.6;dg.update();G[o.name]=geom(o);low=min(p.z for p in G[o.name][0]);probes=[p for p in G[o.name][0] if p.z<low+.0001];detected=[]
for p in probes:
 z=hit(groundtrees[o.get('fixture')],p.x,p.y)
 if z is None or z<p.z-.001:detected.append([list(p),z])
o.location=original_location;dg.update();G[o.name]=original_geo
check('Negative control detects support raised 0.60m',len(probes),[] if detected else ['failed to detect raised support'],.001,{'object':o.name,'intentional_offset_m':.6,'expected_failure_count':len(detected),'expected_failures':detected,'transform_restored_in_memory':True,'blend_saved':False})
geometry_fingerprint=hashlib.sha256(json.dumps([(o.name,[tuple(p) for p in G[o.name][0]],G[o.name][1]) for o in sorted(Y,key=lambda o:o.name)],separators=(',',':')).encode()).hexdigest()
report={'geometry_count_units':{'degenerate_faces':'legacy field: counts original mesh polygons, not triangulated faces','degenerate_polygon_count':'original mesh polygon count below 1e-10 square meters','degenerate_triangle_count':'Blender loop-triangle count below 1e-10 square meters','zero_area_material_uv_polygons':'original mesh polygons with SurfaceUV projected area below 1e-10'},'fixture_geometry_sha256':geometry_fingerprint,'reviewed_file':os.path.basename(bpy.data.filepath),'artifact_filename':'AetherLab_CoreKit_Interface_v2.blend','verification_date_utc':'2026-10-01','sha256':hashlib.sha256(open(bpy.data.filepath,'rb').read()).hexdigest(),'blender':list(bpy.app.version),'scope':'Independent saved-file geometry review. No author validator invoked. No engine/nav/collision/performance claims. Geometry sampling is not exhaustive mesh boolean proof. Presentation floor, labels and character excluded from rays.','tests':tests,'static_capsules':caprows,'master_mesh_review':meshrows,'inherited_unused_master_limits':[r for r in meshrows if r['master'] not in active and (r['degenerate_faces'] or r['zero_area_material_uv_polygons'])],'summary':{'tests':len(tests),'passed':sum(t['passed'] for t in tests),'failed':[t['test'] for t in tests if not t['passed']]}}
json.dump(report,open(REPORT,'w'),ensure_ascii=False,indent=2)
print('INDEPENDENT_SUMMARY',json.dumps(report['summary']));print('UV_ZERO',json.dumps([{ 'master':r['master'],'faces':r['zero_area_material_uv_polygons']} for r in meshrows if r['zero_area_material_uv_polygons']]))

if report["summary"]["failed"]:raise SystemExit(1)
