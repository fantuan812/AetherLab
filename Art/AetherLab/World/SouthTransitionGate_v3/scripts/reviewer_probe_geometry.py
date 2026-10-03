"""Independent read-only geometry checks for the saved transition/gate candidate.
Does not import author code or save a blend; discrete probes are not UE validation.
"""
import bpy, math, json, hashlib, collections, argparse, sys
from mathutils import Vector, Matrix
from mathutils.bvhtree import BVHTree
from pathlib import Path
ap=argparse.ArgumentParser();ap.add_argument('--root',default=str(Path(__file__).resolve().parents[1]));ap.add_argument('--candidate');ap.add_argument('--output-dir');args=ap.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else []);root=Path(args.root);outdir=Path(args.output_dir) if args.output_dir else root/'independent_review';outdir.mkdir(parents=True,exist_ok=True)
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
before=root/'input/AetherLab_Global_World_Blockout_v1.blend'; after=Path(args.candidate) if args.candidate else root/'release/source/AetherLab_Global_World_Blockout_v1.blend'
source_hashes={'before':sha(before),'after':sha(after)}
T='WORLD_Terrain_Continuous_800x800m'
def tree(o):return BVHTree.FromPolygons([o.matrix_world@v.co for v in o.data.vertices],[tuple(p.vertices) for p in o.data.polygons])
def height(b,x,y):
 h=b.ray_cast(Vector((x,y,100)),Vector((0,0,-1)),200)[0]
 return None if h is None else h.z
def bounds(o):
 mw=o.matrix_world
 if o.hide_viewport and o.parent is None:
  mw=Matrix.LocRotScale(o.location,o.rotation_euler.to_quaternion(),o.scale)
 vs=[mw@v.co for v in o.data.vertices]
 return {'min':[min(v[i] for v in vs) for i in range(3)],'max':[max(v[i] for v in vs) for i in range(3)]}
def topo(o):
 mesh=o.data; edges=collections.Counter(); graph=collections.defaultdict(set)
 for p in mesh.polygons:
  ids=list(p.vertices)
  for i,j in zip(ids,ids[1:]+ids[:1]):edges[tuple(sorted((i,j)))]+=1;graph[i].add(j);graph[j].add(i)
 seen=set();components=[]
 for v in range(len(mesh.vertices)):
  if v in seen:continue
  stack=[v];seen.add(v);ct=0
  while stack:
   x=stack.pop();ct+=1
   for y in graph[x]:
    if y not in seen:seen.add(y);stack.append(y)
  components.append(ct)
 bound=[e for e,n in edges.items() if n==1]
 internal=[]
 if o.name==T:
  for e in bound:
   pts=[o.matrix_world@mesh.vertices[i].co for i in e]
   if not any(all(abs(p[axis]-side)<1e-5 for p in pts) for axis in [0,1] for side in [-400,400]):internal.append(list(e))
 return {'vertices':len(mesh.vertices),'faces':len(mesh.polygons),'connected_components':len(components),'component_vertices':components,'boundary_edges':len(bound),'internal_boundary_edges':internal,'nonmanifold_edges_gt2':sum(n>2 for n in edges.values()),'zero_area_faces':sum(p.area<1e-12 for p in mesh.polygons)}
critical=['SCN03_Gate_Pier-4','SCN03_Gate_Pier4','SCN03_Gate_Lintel','SCN03_Gate_Roof','SCN03_Registration_Canopy']
bpy.ops.wm.open_mainfile(filepath=str(before));bpy.context.view_layer.update();baseline={'terrain_topology':topo(bpy.data.objects[T]),'core_bounds':{n:bounds(bpy.data.objects[n]) for n in critical},'foliage':{o.name:{'location':list(o.location),'data':o.data.name} for c in ['08_CONTEXT_FOLIAGE','12_SOUTH_LANDSCAPE_V2'] for o in bpy.data.collections[c].objects if o.type=='MESH' and o.get('master')}}
bpy.ops.wm.open_mainfile(filepath=str(after));bpy.context.view_layer.update();terrain=tree(bpy.data.objects[T]);terrain_topo=topo(bpy.data.objects[T]);core={n:bounds(bpy.data.objects[n]) for n in critical}
roads={o.name:tree(o) for o in bpy.data.objects if o.type=='MESH' and o.name.startswith('ROUTE_C01_Mountain')};verges={o.name:tree(o) for o in bpy.data.objects if o.type=='MESH' and o.name.startswith('LAND3_EarthVerge_')}
foliage=[]
for n,row in baseline['foliage'].items():
 o=bpy.data.objects[n]
 if abs(o.location.z-row['location'][2])>1e-6:
  bottom=min((o.matrix_world@v.co).z for v in o.data.vertices);ground=height(terrain,o.location.x,o.location.y)
  foliage.append({'name':n,'dz':o.location.z-row['location'][2],'xy_unchanged':list(o.location)[:2]==row['location'][:2],'data_unchanged':o.data.name==row['data'],'origin_xy_bottom_gap_m':None if ground is None else bottom-ground})
# Sample both road boundary segments independently. Candidate support includes each earth strip and terrain.
edge_samples=[]
for n,b in roads.items():
 o=bpy.data.objects[n];edge_rec=collections.defaultdict(list)
 for p in o.data.polygons:
  vs=[o.matrix_world@o.data.vertices[i].co for i in p.vertices];cen=sum(vs,Vector())/len(vs)
  for u,v in zip(vs,vs[1:]+vs[:1]):edge_rec[tuple(sorted(tuple(round(c,6) for c in x) for x in (u,v)))].append((u,v,cen))
 for rs in edge_rec.values():
  if len(rs)!=1:continue
  u,v,cen=rs[0];normal=Vector((v.y-u.y,u.x-v.x,0)).normalized()
  if normal.dot((u+v)/2-cen)<0:normal=-normal
  count=max(1,math.ceil((v-u).length/.20))
  # Mid-cell probes avoid repeatedly counting common endpoints.
  for j in range(count):
   p=u.lerp(v,(j+.5)/count)
   if not -372<p.y<-96:continue
   for off in [.01,.05,.25,1.0,1.55]:
    q=p+normal*off;ground=height(terrain,q.x,q.y);vh={name:height(tr,q.x,q.y) for name,tr in verges.items()};heights=[(ground,T)]+[(z,name) for name,z in vh.items() if z is not None];heights=[r for r in heights if r[0] is not None];support=max(heights) if heights else (None,None)
    road_hits=[(height(rb,q.x,q.y),rn) for rn,rb in roads.items()];road_hits=[x for x in road_hits if x[0] is not None];road_support=max(road_hits) if road_hits else (None,None);all_support=max(heights+road_hits) if heights+road_hits else (None,None)
    edge_samples.append({'road':n,'xy':[q.x,q.y],'offset_m':off,'raw_step_m':None if ground is None else p.z-ground,'effective_step_m':None if support[0] is None else p.z-support[0],'support':support[1],'target_road_at_probe':road_support[1],'effective_including_target_roads_m':None if all_support[0] is None else p.z-all_support[0],'all_support':all_support[1]})
summ={}
for off in [.01,.05,.25,1.0,1.55]:
 rows=[r for r in edge_samples if r['offset_m']==off]; good=[r for r in rows if r['effective_step_m'] is not None];summ[str(off)]={'count':len(rows),'missing':len(rows)-len(good),'raw_min_m':min(r['raw_step_m'] for r in good),'raw_max_m':max(r['raw_step_m'] for r in good),'effective_min_m':min(r['effective_step_m'] for r in good),'effective_max_m':max(r['effective_step_m'] for r in good),'abs_effective_over_0p22':sum(abs(r['effective_step_m'])>.22 for r in good),'worst_raised_sample':min(good,key=lambda r:r['effective_step_m']),'worst_lower_sample':max(good,key=lambda r:r['effective_step_m']),'probes_on_saved_target_road':sum(r['target_road_at_probe'] is not None for r in good),'including_target_roads_min_m':min(r['effective_including_target_roads_m'] for r in good),'including_target_roads_max_m':max(r['effective_including_target_roads_m'] for r in good),'including_target_roads_abs_over_0p22':sum(abs(r['effective_including_target_roads_m'])>.22 for r in good),'worst_lower_including_target_roads':max(good,key=lambda r:r['effective_including_target_roads_m'])}
for off in [.01,.05,.25,1.0,1.55]:
 good=[r for r in edge_samples if r['offset_m']==off and r['effective_including_target_roads_m'] is not None]
 outside=[r for r in good if r['target_road_at_probe'] is None]
 summ[str(off)]['including_target_roads_mean_m']=sum(r['effective_including_target_roads_m'] for r in good)/len(good)
 summ[str(off)]['off_road_only']={'count':len(outside),'min_m':min(r['effective_including_target_roads_m'] for r in outside),'max_m':max(r['effective_including_target_roads_m'] for r in outside),'mean_m':sum(r['effective_including_target_roads_m'] for r in outside)/len(outside)}
# Shoulder face-center overlaps with either saved road surface, and amount of protrusion above it.
overlap=[]
for n in verges:
 o=bpy.data.objects[n]
 for p in o.data.polygons:
  q=o.matrix_world@p.center
  hits=[(height(tr,q.x,q.y),rn) for rn,tr in roads.items()];hits=[x for x in hits if x[0] is not None]
  if hits:
   z,rn=max(hits);overlap.append({'verge':n,'face':p.index,'xyz':list(q),'road':rn,'above_road_m':q.z-z})
# Opening face bounds plus densely sampled vertical column rays through visible gate collection.
gate_objs=[o for o in bpy.data.objects if o.type=='MESH' and not o.hide_render and (o.name.startswith('SCN03_Gate') or o.name.startswith('GATE3_')) and not all(c.hide_render for c in o.users_collection)]
gate_trees={o.name:tree(o) for o in gate_objs};opening=[]
for x in [-2.49+i*(4.98/50) for i in range(51)]:
 for y in [-83.0+i*.1 for i in range(61)]:
  hits=[]
  for n,tr in gate_trees.items():
   h=tr.ray_cast(Vector((x,y,.02)),Vector((0,0,1)),6)[0]
   if h is not None:hits.append((h.z,n))
  if hits:
   z,n=min(hits)
   if z<5.99:opening.append({'x':x,'y':y,'z':z,'object':n})
# Verify existing registration roof is linked, unscaled and lowest vertex 3.4m.
can=bpy.data.objects['GATE3_Registration_AuthoredRoof'];master=bpy.data.objects['SRC_Roof_H03']
report={'reviewer_script_sha256':sha(__file__),'source_hashes':source_hashes,'source_unchanged':source_hashes=={'before':sha(before),'after':sha(after)},'method':'Independent saved-file reopen, raw mesh topology, transformed mesh bounds and discrete BVH rays; no author code imported and no scene saved. No UE compilation or tests.','baseline':{'terrain_topology':baseline['terrain_topology'],'core_bounds':baseline['core_bounds']},'candidate':{'terrain_topology':terrain_topo,'core_bounds':core,'verge_topology':{n:topo(bpy.data.objects[n]) for n in verges}},'foliage':{'changed_count':len(foliage),'all_xy_and_mesh_links_retained':all(r['xy_unchanged'] and r['data_unchanged'] for r in foliage),'max_abs_origin_xy_bottom_gap_m':max(abs(r['origin_xy_bottom_gap_m']) for r in foliage),'limitation':'Origin-XY bottom-height contact only; not every leaf/branch or entire footprint contact'},'edge_probes':summ,'shoulder_face_centers_over_road':{'count':len(overlap),'above_by_over_0p01_count':sum(r['above_road_m']>.01 for r in overlap),'maximum_protrusion_m':max((r['above_road_m'] for r in overlap),default=None),'highest':sorted(overlap,key=lambda r:r['above_road_m'],reverse=True)[:12]},'gate':{'original_bounds_retained':{n:baseline['core_bounds'][n]==core[n] for n in critical},'opening_width_from_piers_m':core['SCN03_Gate_Pier4']['min'][0]-core['SCN03_Gate_Pier-4']['max'][0],'opening_lintel_underside_z_m':core['SCN03_Gate_Lintel']['min'][2],'vertical_ray_sample_count':51*61,'new_geometry_in_opening_below_5p99m':opening,'limit':'Sampled mesh clearance, not navigation or collision capsule sweep'},'registration_roof':{'same_mesh_as_source':can.data==master.data,'scale':list(can.scale),'bounds':bounds(can)},'limits':['Discrete samples may miss between-sample defects','Open sheet terrain/shoulders are expected to have exterior boundary edges','No UE, physics, navigation or final whole-world acceptance','Positive edge step means support is below the road; negative means raised support and may reflect intersecting road/shoulder grades']}
(outdir/'Geometry_Probe.json').write_text(json.dumps(report,ensure_ascii=False,indent=2));print(json.dumps({k:v for k,v in report.items() if k not in ['baseline','candidate','edge_probes']},ensure_ascii=False,indent=2))
