"""Read-only discrete contact probes along actual saved shoulder boundary edges."""
import bpy,json,hashlib,collections,math,argparse,sys
from pathlib import Path
from mathutils import Vector
from mathutils.bvhtree import BVHTree
ap=argparse.ArgumentParser();ap.add_argument('--root',default=str(Path(__file__).resolve().parents[1]));ap.add_argument('--candidate');ap.add_argument('--output-dir');args=ap.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else []);r=Path(args.root);outdir=Path(args.output_dir) if args.output_dir else r/'independent_review';outdir.mkdir(parents=True,exist_ok=True);source=Path(args.candidate) if args.candidate else r/'release/source/AetherLab_Global_World_Blockout_v1.blend';sha=lambda f:hashlib.sha256(Path(f).read_bytes()).hexdigest();s=sha(source)
bpy.ops.wm.open_mainfile(filepath=str(source));bpy.context.view_layer.update()
def bt(o):return BVHTree.FromPolygons([o.matrix_world@v.co for v in o.data.vertices],[tuple(p.vertices) for p in o.data.polygons])
def zhit(t,x,y):
 h=t.ray_cast(Vector((x,y,100)),Vector((0,0,-1)),200)[0]
 return None if h is None else h.z
terrain=bt(bpy.data.objects['WORLD_Terrain_Continuous_800x800m']);rv=[];rf=[]
for o in bpy.data.objects:
 if o.type=='MESH' and o.name.startswith('ROUTE_C01_Mountain'):
  base=len(rv);rv.extend(o.matrix_world@v.co for v in o.data.vertices);rf.extend(tuple(base+i for i in p.vertices) for p in o.data.polygons)
rt=BVHTree.FromPolygons(rv,rf);flat=BVHTree.FromPolygons([(v.x,v.y,0) for v in rv],rf)
rows=[]
for o in bpy.data.objects:
 if o.type!='MESH' or not o.name.startswith('LAND3_EarthVerge_'):continue
 edges=collections.Counter()
 for p in o.data.polygons:
  ids=list(p.vertices)
  for a,b in zip(ids,ids[1:]+ids[:1]):edges[tuple(sorted((a,b)))]+=1
 for (a,b),ct in edges.items():
  if ct!=1:continue
  p=o.matrix_world@o.data.vertices[a].co;q=o.matrix_world@o.data.vertices[b].co
  for t in [.1,.5,.9]:
   v=p.lerp(q,t);nearest=flat.find_nearest(Vector((v.x,v.y,0)));dist=nearest[3];tz=zhit(terrain,v.x,v.y);rz=zhit(rt,v.x,v.y)
   if rz is None and dist is not None and dist<.005:
    # Offset the nearest planar point slightly toward its face center to avoid
    # floating point boundary misses. This is a discrete neighbor grade probe.
    loc,normal,idx,_=nearest;pts=[rv[i] for i in rf[idx]];c=sum(pts,Vector())/len(pts);v2=loc.lerp(Vector((c.x,c.y,0)),.0001);rz=zhit(rt,v2.x,v2.y)
   rows.append({'verge':o.name,'xyz':list(v),'nearest_road_xy_distance_m':dist,'road_z':rz,'terrain_z':tz,'above_road_m':None if rz is None else v.z-rz,'above_terrain_m':None if tz is None else v.z-tz})
inner=[x for x in rows if x['nearest_road_xy_distance_m'] is not None and x['nearest_road_xy_distance_m']<.01];outer=[x for x in rows if x['nearest_road_xy_distance_m'] is not None and x['nearest_road_xy_distance_m']>.1]
def summary(xs,key):
 a=[x for x in xs if x[key] is not None]
 return {'samples':len(xs),'known':len(a),'min_m':min((x[key] for x in a),default=None),'max_m':max((x[key] for x in a),default=None),'abs_over_0p02m_count':sum(abs(x[key])>.02 for x in a),'highest':sorted(a,key=lambda x:x[key],reverse=True)[:6],'lowest':sorted(a,key=lambda x:x[key])[:6]}
report={'reviewer_script_sha256':sha(__file__),'source_sha256':s,'source_unchanged':s==sha(source),'method':'For each actual shoulder topological boundary edge, probe 10%,50%,90% positions against world-space terrain and the two target mountain-route BVHs; road-adjacent if distance<1cm, exterior-like if road distance>10cm. Endpoint/cut-end boundary faces may be included. No scene save or UE.','samples':len(rows),'road_adjacent_boundary':summary(inner,'above_road_m'),'exterior_like_boundary':summary(outer,'above_terrain_m'),'outer_ring_approx_1p5m':summary([x for x in rows if x['nearest_road_xy_distance_m'] is not None and 1.4<x['nearest_road_xy_distance_m']<1.6],'above_terrain_m'),'limits':['Discrete samples, not a continuous crack/overlap certificate','Exterior-like includes strip ends and may include transition boundary; distances are horizontal','No evaluation of collision/navmesh/runtime traversability']};(outdir/'Verge_Boundary_Probe.json').write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2))
