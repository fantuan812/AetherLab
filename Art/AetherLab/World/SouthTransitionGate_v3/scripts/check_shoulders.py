"""Reopen actual meshes, probe the persisted south route outer edges and ground."""
import bpy,json,math,hashlib,argparse,sys,collections
from pathlib import Path
from mathutils import Vector
from mathutils.bvhtree import BVHTree
p=argparse.ArgumentParser();p.add_argument('--source',required=True);p.add_argument('--output',required=True);a=p.parse_args(sys.argv[sys.argv.index('--')+1:]);sha=lambda f:hashlib.sha256(Path(f).read_bytes()).hexdigest();sh=sha(a.source);bpy.ops.wm.open_mainfile(filepath=str(Path(a.source).resolve()));bpy.context.view_layer.update()
t=bpy.data.objects['WORLD_Terrain_Continuous_800x800m'];tb=BVHTree.FromPolygons([t.matrix_world@v.co for v in t.data.vertices],[tuple(p.vertices) for p in t.data.polygons]);rows=[]
verge_trees=[]
for vo in bpy.data.objects:
 if vo.name.startswith('LAND3_EarthVerge_'):
  verge_trees.append(BVHTree.FromPolygons([vo.matrix_world@v.co for v in vo.data.vertices],[tuple(p.vertices) for p in vo.data.polygons]))
for name in ['ROUTE_C01_Mountain_Main','ROUTE_C01_Mountain_Rescue_Bypass']:
 o=bpy.data.objects[name];edges=collections.defaultdict(list)
 for f in o.data.polygons:
  vs=[o.matrix_world@o.data.vertices[i].co for i in f.vertices];center=sum(vs,Vector())/len(vs)
  for v,w in zip(vs,vs[1:]+vs[:1]):edges[tuple(sorted([tuple(round(c,5) for c in v),tuple(round(c,5) for c in w)]))].append((v,w,center))
 for records in edges.values():
  if len(records)!=1:continue
  v,w,center=records[0];direction=w-v;L=Vector((direction.x,direction.y)).length
  if L<.001:continue
  normal=Vector((-direction.y,direction.x,0)).normalized()
  if normal.dot((v+w)*.5-center)<0:normal=-normal
  for j in range(math.ceil(L/.25)+1):
   pos=v+(w-v)*(j/math.ceil(L/.25))
   if not -372<pos.y<-96:continue
   for offset in [-.02,.05,.25,1]:
    q=pos+normal*offset;hit=tb.ray_cast(Vector((q.x,q.y,100)),Vector((0,0,-1)),200)[0]
    vh=[b.ray_cast(Vector((q.x,q.y,100)),Vector((0,0,-1)),200)[0] for b in verge_trees];support=max([hh.z for hh in [hit]+vh if hh is not None],default=None)
    rows.append({'effective_step_m':None if support is None else pos.z-support,'route':name,'p':list(q),'offset_m':offset,'terrain_z':None if hit is None else hit.z,'road_edge_z':pos.z,'step_m':None if hit is None else pos.z-hit.z})
summary={}
for off in [-.02,.05,.25,1]:
 vals=[r['step_m'] for r in rows if r['offset_m']==off and r['step_m'] is not None];summary[str(off)]={'count':len(vals),'min_road_above_terrain_m':min(vals),'max_road_above_terrain_m':max(vals),'mean_road_above_terrain_m':sum(vals)/len(vals),'abs_over_0p22m':sum(abs(v)>.22 for v in vals)}
effective={}
for off in [-.02,.05,.25,1]:
 vals=[r['effective_step_m'] for r in rows if r['offset_m']==off and r['effective_step_m'] is not None];effective[str(off)]={'count':len(vals),'min_m':min(vals),'max_m':max(vals),'mean_m':sum(vals)/len(vals),'abs_over_0p22m':sum(abs(v)>.22 for v in vals)}
report={'effective_terrain_and_verge_summary':effective,'source_sha256':sh,'source_unchanged':sh==sha(a.source),'method':'Actual saved road face boundary edges position-welded at 1e-5m, boundary samples <=0.25m apart, independent world-space terrain raycasts. Bounded y(-372,-96); offsets outward based on polygon center. Not continuous sweep or UE validation.','summary':summary,'missing_ground':sum(r['step_m'] is None for r in rows),'samples':rows}
Path(a.output).write_text(json.dumps(report,indent=2));print(json.dumps(summary),flush=True)
