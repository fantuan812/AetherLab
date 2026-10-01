"""Read-only acceptance review of reopened, evaluated world meshes.
No import of author code, socket coordinates, manifests or author validators.
Only role and collection tags select objects; all measurements come from meshes.
Never saves or changes the .blend on disk. Perturbations are in memory and restored.
"""
import bpy,bmesh,json,math,hashlib,os,sys,datetime,argparse
from mathutils import Vector
from mathutils.bvhtree import BVHTree
ROOT=os.path.dirname(os.path.abspath(__file__))
SOURCE=bpy.data.filepath
parser=argparse.ArgumentParser(description='Independent read-only transition geometry review')
parser.add_argument('--output-report',default=os.path.join(ROOT,'Independent_Geometry_Review.json'))
args=parser.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
OUTPUT_REPORT=os.path.abspath(args.output_report)
os.makedirs(os.path.dirname(OUTPUT_REPORT),exist_ok=True)
EPS=1e-5
COL='06_CONNECTED_TRANSITION_FIXTURE__NOT_WORLD'
MAST='05_TRANSITION_MASTERS__CANDIDATES'
report={'source_file':os.path.basename(SOURCE),'source_sha256':hashlib.sha256(open(SOURCE,'rb').read()).hexdigest(),'reviewer':'independent world-mesh reopening audit','reviewed_at_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'method':'Evaluated actual meshes and matrix_world; BVH ray intersections, mesh topology, transformed face and barycentric samples. No author validators, socket declarations or expected-placement coordinate comparisons.','scope':COL,'tolerances_m':{'contact':0.001,'seam_height':0.001,'route_max_sample_step':0.13},'limitations':['Static geometry evidence only; no UE, navigation, controller, performance, physics, structural load or accessibility conclusions.','SurfaceUV validates existence, finite coordinates and nonzero triangle UV area; no unique atlas, texel-density or artistic UV approval.','Rendered images are not automatically inspected by this script; see the separate independent visual review.']}

class World:
 def __init__(self,o):
  self.o=o;self.name=o.name;self.role=o.get('role','');dg=bpy.context.evaluated_depsgraph_get();ev=o.evaluated_get(dg);m=ev.to_mesh();m.calc_loop_triangles()
  self.v=[ev.matrix_world@v.co for v in m.vertices];self.tri=[tuple(t.vertices) for t in m.loop_triangles]
  self.bvh=BVHTree.FromPolygons(self.v,self.tri,all_triangles=True,epsilon=0.0)
  self.lo=Vector([min(v[k] for v in self.v) for k in range(3)]);self.hi=Vector([max(v[k] for v in self.v) for k in range(3)])
  self.mid=(self.lo+self.hi)*.5;ev.to_mesh_clear()
 def hit(self,p,d,maxd=100):
  co,no,idx,dist=self.bvh.ray_cast(Vector(p),Vector(d),maxd)
  return None if co is None else (co,no,idx,dist)
 def top(self,x,y):
  if x<self.lo.x-EPS or x>self.hi.x+EPS or y<self.lo.y-EPS or y>self.hi.y+EPS:return None
  h=self.hit((x,y,self.hi.z+1),(0,0,-1),self.hi.z-self.lo.z+2)
  return None if h is None else h[0].z
 def bottom(self,x,y):
  h=self.hit((x,y,self.lo.z-1),(0,0,1),self.hi.z-self.lo.z+2)
  return None if h is None else h[0].z
 def inside(self,p):
  p=Vector(p)
  nearest=self.bvh.find_nearest(p)
  return nearest[0] is not None and (p-nearest[0]).dot(nearest[1]) < -EPS

def worlds():return [World(o) for o in bpy.data.collections[COL].objects if o.type=='MESH']
def role(ws,*roles):return [w for w in ws if w.role in roles]
def top(ws,x,y):
 hits=[(z,w.name) for w in ws if (z:=w.top(x,y)) is not None]
 return max(hits) if hits else (None,None)
def sequence(a,b,n):return [a+(b-a)*i/(n-1) for i in range(n)]
def closest_face_sample(w):
 for t in w.tri:
  a,b,c=[w.v[i] for i in t];n=(b-a).cross(c-a)
  if n.length<EPS:continue
  yield a,b,c,n.normalized()
def sample_tri(a,b,c,n=4):
 # Strict interior samples avoid ambiguous shared edges.
 for i in range(n):
  for j in range(n-i):
   u=(i+1/3)/n;v=(j+1/3)/n
   if u+v<1:yield a*(1-u-v)+b*u+c*v

# Integrity and real sharing checks.
old=bpy.data.collections['01_MASTER_MODULES__4m_Candidate']
oldmats={m.as_pointer() for o in old.objects if o.type=='MESH' for m in o.data.materials if m}
master_results=[]
for o in bpy.data.collections[MAST].objects:
 if o.type!='MESH':continue
 m=o.data;m.calc_loop_triangles();bm=bmesh.new();bm.from_mesh(m)
 uv=m.uv_layers.get('SurfaceUV');uvzero=[];badfinite=0
 if uv:
  for t in m.loop_triangles:
   a,b,c=[uv.data[l].uv for l in t.loops]
   ar=abs((b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x))*.5
   if ar<1e-12:uvzero.append(t.index)
   badfinite+=sum(not math.isfinite(v) for p in (a,b,c) for v in p)
 degtris=[t.index for t in m.loop_triangles if t.area<1e-10]
 item={'master':o.name,'mesh':m.name,'vertices':len(m.vertices),'faces':len(m.polygons),'triangles':len(m.loop_triangles),'signed_volume_local_m3':bm.calc_volume(signed=True),'nonmanifold_edges':sum(not e.is_manifold for e in bm.edges),'inconsistent_winding_edges':sum(e.is_manifold and not e.is_contiguous for e in bm.edges),'zero_length_edges':sum(e.calc_length()<1e-8 for e in bm.edges),'degenerate_faces':sum(f.calc_area()<1e-10 for f in bm.faces),'degenerate_triangles':degtris,'surfaceuv':{'exists':bool(uv),'nonfinite_values':badfinite,'zero_area_triangles':uvzero},'materials':[m.name for m in m.materials],'materials_are_existing_shared_datablocks':all(x.as_pointer() in oldmats for x in m.materials),'linked_fixture_instances':[i.name for i in bpy.data.collections[COL].objects if i.type=='MESH' and i.data is m]}
 item['pass']=item['signed_volume_local_m3']>0 and not any(item[k] for k in ['nonmanifold_edges','inconsistent_winding_edges','zero_length_edges','degenerate_faces']) and not degtris and bool(uv) and not uvzero and not badfinite and item['materials_are_existing_shared_datablocks'] and len(item['linked_fixture_instances'])>0
 master_results.append(item);bm.free()
report['masters']=master_results
ws=worlds();byname={w.name:w for w in ws}
report['fixture_instance_count']=len(ws)
report['linked_instance_check']={'unlinked_or_wrong_master':[w.name for w in ws if not bpy.data.objects.get(w.o.get('master','')) or w.o.data is not bpy.data.objects[w.o['master']].data],'non_unit_scale':[w.name for w in ws if max(abs(v-1) for v in w.o.scale)>EPS]}
report['hidden_legacy_groups']={c:bool(bpy.data.collections[c].hide_render and bpy.data.collections[c].hide_viewport) for c in ['02_CONNECTION_FIXTURES__NOT_WORLD','03_LABELS_AND_SCALE','04_SOCKET_GUIDES']}

# New-mother metadata schema audit also covers all linked copies, independently of geometry.
allowed_metadata={'forward_up','socket_semantics','asset_id','role','derived_from','stage','local_sockets_json','allowed_instance_scale'}
metadata_rows=[]
for master in bpy.data.collections[MAST].objects:
 linked=[o for o in bpy.data.collections[COL].objects if o.type=='MESH' and o.data is master.data]
 for o in [master]+linked:
  allowed=allowed_metadata if o is master else allowed_metadata|{'master','fixture'}
  keys=set(o.keys());extra=sorted(keys-allowed);missing=sorted(allowed-keys)
  mismatch=[] if o is master else [k for k in allowed_metadata if o.get(k)!=master.get(k)]
  meshkeys=list(o.data.keys())
  try:valid_sockets=isinstance(json.loads(o['local_sockets_json']),dict)
  except:valid_sockets=False
  metadata_rows.append({'object':o.name,'master':master.name,'unexpected_keys':extra,'missing_keys':missing,'master_value_mismatches':mismatch,'mesh_property_keys':meshkeys,'pass':not extra and not missing and not mismatch and not meshkeys and valid_sockets and o.get('forward_up')=='+Y/+Z' and o.get('allowed_instance_scale')=='1,1,1'})
report['new_metadata_schema']={'expected_master_keys':sorted(allowed_metadata),'objects':metadata_rows,'pass':all(x['pass'] for x in metadata_rows),'note':'Excludes conflicting inherited grid_m/connectors_json/ROAD/EARTH and any other old-schema keys on the 6 new masters and 25 linked copies.'}

# Actual section profiles derive the sample points from their world-space footprint.
st=role(ws,'stair')[0];xc=st.mid.x
stys=sequence(st.lo.y+.001,st.hi.y-.001,801)
sttops=[st.top(xc,y) for y in stys]
levels=[]
for y,z in zip(stys,sttops):
 if not levels or abs(z-levels[-1]['height_m'])>1e-4:levels.append({'start_y_m':y,'height_m':z})
roads=role(ws,'road_support')
low=max([w for w in roads if w.hi.y<=st.lo.y+EPS],key=lambda w:w.hi.y)
high=min([w for w in roads if w.lo.y>=st.hi.y-EPS],key=lambda w:w.lo.y)
xs=sequence(max(st.lo.x,low.lo.x)+.01,min(st.hi.x,low.hi.x)-.01,41)
bottom=[{'x':x,'lower_top':low.top(x,low.hi.y-.0001),'first_tread':st.top(x,st.lo.y+.0001)} for x in xs]
upper=[{'x':x,'last_tread':st.top(x,st.hi.y-.0001),'upper_bed':high.top(x,high.lo.y+.0001)} for x in xs]
lowrises=[v['first_tread']-v['lower_top'] for v in bottom]
upperdelta=[v['upper_bed']-v['last_tread'] for v in upper]
rises=lowrises[:1]+[levels[i]['height_m']-levels[i-1]['height_m'] for i in range(1,len(levels))]
report['stair']={'objects':[low.name,st.name,high.name],'tread_level_count':len(levels),'tread_levels_m':[v['height_m'] for v in levels],'rises_m':rises,'lower_mouth_horizontal_gap_m':st.lo.y-low.hi.y,'lower_mouth_rise_range_m':[min(lowrises),max(lowrises)],'upper_mouth_horizontal_gap_m':high.lo.y-st.hi.y,'upper_mouth_height_delta_range_m':[min(upperdelta),max(upperdelta)],'cross_section_samples_per_mouth':len(xs),'pass':len(levels)==8 and abs(st.lo.y-low.hi.y)<.001 and abs(high.lo.y-st.hi.y)<.001 and max(abs(v) for v in upperdelta)<.001 and all(0<v<=.13 for v in rises)}

# End-to-end deck/bank seam checks use sorted actual world footprints and ray heights.
def bridge_check(wss):
 ds=sorted(role(wss,'bridge_deck'),key=lambda w:w.mid.y);banks=sorted(role(wss,'bridge_abutment'),key=lambda w:w.mid.y);chain=[banks[0]]+ds+[banks[-1]];seams=[]
 for a,b in zip(chain,chain[1:]):
  xx=sequence(max(a.lo.x,b.lo.x)+.01,min(a.hi.x,b.hi.x)-.01,41);dh=[]
  for x in xx:
   za=a.top(x,a.hi.y-.0001);zb=b.top(x,b.lo.y+.0001)
   if za is not None and zb is not None:dh.append(zb-za)
  gap=b.lo.y-a.hi.y
  seams.append({'a':a.name,'b':b.name,'gap_m':gap,'height_delta_max_abs_m':max(abs(x) for x in dh) if dh else None,'samples':len(dh),'pass':abs(gap)<.001 and bool(dh) and max(abs(x) for x in dh)<.001})
 return {'seams':seams,'pass':all(s['pass'] for s in seams)}
report['bridge_chain']=bridge_check(ws)

# Bearing face contact: sample the real beam upper triangles; bank embedding from true point-inside tests.
bearers=role(ws,'bridge_bearer');banks=role(ws,'bridge_abutment');decks=role(ws,'bridge_deck');bearing=[]
for beam in bearers:
 contacts=[]
 for deck in decks:
  samples=[]
  xlo=max(beam.lo.x,deck.lo.x);xhi=min(beam.hi.x,deck.hi.x);ylo=max(beam.lo.y,deck.lo.y);yhi=min(beam.hi.y,deck.hi.y)
  if xlo<xhi and ylo<yhi:
   for x in sequence(xlo+.001,xhi-.001,5):
    for y in sequence(ylo+.001,yhi-.001,5):
     z1=beam.top(x,y);z2=deck.bottom(x,y)
     if z1 is not None and z2 is not None:samples.append(z2-z1)
  contacts.append({'deck':deck.name,'sample_count':len(samples),'gap_max_abs_m':max(abs(v) for v in samples) if samples else None,'pass':len(samples)==25 and max(abs(v) for v in samples)<.001})
 embeds=[]
 for bank in banks:
  # The beam centreline is sampled inside the actual bank volume in 1mm increments.
  ys=sequence(beam.lo.y+.0005,beam.hi.y-.0005,4500);inside=[y for y in ys if bank.inside((beam.mid.x,y,beam.mid.z))]
  embeds.append({'bank':bank.name,'actual_interior_sample_count':len(inside),'embedded_length_m':(max(inside)-min(inside)+.001) if inside else 0,'pass':len(inside)>200})
 bearing.append({'beam':beam.name,'deck_contact':contacts,'bank_embedding':embeds,'pass':all(s['pass'] for s in contacts+embeds)})
report['bridge_bearers']=bearing

# Road-surface bottom triangles must intersect backing, with no floating disconnected flagstone pieces.
backing=role(ws,'road_support','ground');paving_results=[]
for paving in role(ws,'road_surface'):
 samples=[];failed=[]
 for a,b,c,n in closest_face_sample(paving):
  if n.z<-.1:
   for p in sample_tri(a,b,c,2):
    hit=top(backing,p.x,p.y)
    if hit[0] is None or hit[0]<p.z-.001:failed.append({'point':list(p),'backing_top':hit[0],'backing':hit[1]})
    samples.append(p)
 paving_results.append({'paving':paving.name,'lower_surface_sample_count':len(samples),'unsupported_sample_count':len(failed),'failure_examples':failed[:8],'pass':len(samples)>0 and not failed})
report['paving_backing']=paving_results
# Substrate/bank contact uses actual bottom face samples, distinguishing integral foundations.
subresults=[]
for bed in role(ws,'road_support'):
 candidates=[w for w in role(ws,'ground') if w.hi.x>bed.lo.x and w.lo.x<bed.hi.x and w.hi.y>bed.lo.y+.001 and w.lo.y<bed.hi.y-.001]
 # A full-depth solid footing is itself the sample's foundation; compare ground datum from other ground mesh bottoms.
 groundbase=min(w.lo.z for w in role(ws,'ground'))
 integral=abs(bed.lo.z-groundbase)<.001
 if integral:subresults.append({'bed':bed.name,'mode':'integral solid foundation reaches terrain base','bottom_z_m':bed.lo.z,'terrain_base_z_m':groundbase,'pass':True});continue
 vals=[];fails=[]
 for a,b,c,n in closest_face_sample(bed):
  if n.z<-.1:
   for p in sample_tri(a,b,c,6):
    ok=any(w.inside(p+Vector((0,0,.0002))) or (w.top(p.x,p.y) is not None and w.top(p.x,p.y)>=p.z-.001 and w.lo.z<=p.z) for w in candidates)
    vals.append(ok)
    if not ok:fails.append(list(p))
 subresults.append({'bed':bed.name,'mode':'substrate backed by actual bank volume','bank_objects':[w.name for w in candidates],'samples':len(vals),'unsupported_samples':len(fails),'failure_examples':fails[:8],'pass':bool(vals) and all(vals)})
report['foundation_backing']=subresults

# Rock bottom is measured at mesh vertices and lower face points against real soil geometry.
soil=role(ws,'road_support','ground','rock_toe');rockresults=[]
for rock in role(ws,'rock'):
 deepest=min(rock.v,key=lambda v:v.z);depthsoil=[w for w in soil if w.inside(deepest)]
 surface_samples=[];unbacked=[];embedded=[]
 for a,b,c,n in closest_face_sample(rock):
  if n.z<-.2:
   for p in sample_tri(a,b,c,5):
    z,which=top(soil,p.x,p.y)
    if z is None:unbacked.append(list(p))
    if any(w.inside(p) for w in soil):embedded.append(list(p))
    surface_samples.append(p)
 soil_top,soil_name=top(soil,deepest.x,deepest.y)
 rockresults.append({'rock':rock.name,'deepest_vertex_m':list(deepest),'soil_at_deepest_vertex':[w.name for w in depthsoil],'soil_surface_above_deepest_m':soil_top-deepest.z if soil_top is not None else None,'lower_face_samples':len(surface_samples),'embedded_lower_face_samples':len(embedded),'lower_face_samples_with_no_soil_below':len(unbacked),'unsupported_example_points':unbacked[:8],'pass':bool(depthsoil) and bool(embedded),'note':'Natural lower side overhang is measured and disclosed; deepest-base actual soil embedding is the acceptance target.'})
report['rock_ground']=rockresults

# A route is determined from the actual supporting meshes, not authored coordinates.
walk=role(ws,'road_surface','road_support','stair','bridge_abutment','bridge_deck')
ymin=min(w.lo.y for w in walk);ymax=max(w.hi.y for w in walk);center=st.mid.x
root=bpy.data.objects['SCALE_REFERENCE_TRANSLATION_ONLY'];char=[o for o in root.children_recursive if o.type=='MESH'];cv=[o.matrix_world@v.co for o in char for v in o.data.vertices]
clo=Vector([min(v[k] for v in cv) for k in range(3)]);chi=Vector([max(v[k] for v in cv) for k in range(3)]);dim=chi-clo
report['legacy_reference']={'mesh_objects':[o.name for o in char],'world_bounds_m':[list(clo),list(chi)],'actual_dimensions_m':list(dim),'actual_height_m':dim.z,'changes_by_reviewer':'none'}
# Cross-route lines include the entire actual old T-pose width. New character width is not supplied.
xss=sequence(center-dim.x*.5,center+dim.x*.5,17);yss=sequence(ymin+.001,ymax-.001,1361)
route=[];missing=[]
for y in yss:
 zz=[top(walk,x,y)[0] for x in xss]
 if any(z is None for z in zz):missing.append(y)
 route.append((y,zz))
maxstep=0;loc=None
for (y0,z0),(y1,z1) in zip(route,route[1:]):
 for x,a,b in zip(xss,z0,z1):
  if a is not None and b is not None and abs(b-a)>maxstep:maxstep=abs(b-a);loc=[x,y0,y1,a,b]
clearances=[]
for height in [1.65,1.8028]:
 collisions=[];count=0
 for y,zz in route:
  for x,z in zip(xss,zz):
   if z is None:continue
   count+=1
   for w in ws:
    # Cast upwards from just above the actual floor, including all fixture objects.
    h=w.hit((x,y,z+.002),(0,0,1),height-.002)
    if h is not None:collisions.append({'position':[x,y,z],'obstacle':w.name,'height_above_floor_m':h[0].z-z})
 clearances.append({'height_m':height,'sampled_width_m':dim.x,'vertical_probe_count':count,'collision_count':len(collisions),'examples':collisions[:10],'pass':not collisions})
report['static_route']={'derived_center_x_m':center,'derived_extent_y_m':[ymin,ymax],'longitudinal_samples':len(yss),'transverse_samples':len(xss),'max_sample_spacing_m':(ymax-ymin-.002)/(len(yss)-1),'sampled_width_m':dim.x,'missing_floor_columns':len(missing),'max_adjacent_height_step_m':maxstep,'largest_step_location':loc,'height_clearances':clearances,'pass':not missing and maxstep<=.13 and all(c['pass'] for c in clearances),'note':'Straight static clearance sampling uses full unchanged legacy T-pose width, and separate 1.65m / 1.8028m height tests. It does not assert dynamic traversal or a new-character capsule.'}
# Clear width measured from inner obstacle extrema above floor.
widths=[]
for group in ['STAIR','BRIDGE']:
 obs=[w for w in ws if w.role in ['post','rail'] and w.name.startswith(group)]
 left=[w for w in obs if w.mid.x<center];right=[w for w in obs if w.mid.x>center]
 widths.append({'segment':group,'minimum_obstacle_inner_width_m':min(w.lo.x for w in right)-max(w.hi.x for w in left)})
report['static_route']['rail_inner_widths']=widths

# Real position perturbation: move a deck 0.2m in world Z, rebuild its world BVH, rerun identical seam test.
target=sorted(role(ws,'bridge_deck'),key=lambda w:w.mid.y)[len(decks)//2].o
saved=target.matrix_world.copy();baseline=report['bridge_chain']['pass']
target.location.z+=.2;bpy.context.view_layer.update();perturbed=bridge_check(worlds());target.matrix_world=saved;bpy.context.view_layer.update();restored=bridge_check(worlds())
report['negative_control']={'object':target.name,'actual_transform_perturbation_m':[0,0,.2],'baseline_pass':baseline,'perturbed_pass':perturbed['pass'],'detected_failures':[s for s in perturbed['seams'] if not s['pass']],'restored_pass':restored['pass'],'expected_failure_observed':baseline and not perturbed['pass'] and restored['pass'],'source_file_saved':False}
required=[report['new_metadata_schema']['pass'],all(x['pass'] for x in report['masters']),not report['linked_instance_check']['unlinked_or_wrong_master'],not report['linked_instance_check']['non_unit_scale'],report['stair']['pass'],report['bridge_chain']['pass'],all(x['pass'] for x in bearing),all(x['pass'] for x in paving_results),all(x['pass'] for x in subresults),all(x['pass'] for x in rockresults),report['static_route']['pass'],report['negative_control']['expected_failure_observed']]
report['geometry_gate_pass']=all(required)
json.dump(report,open(OUTPUT_REPORT,'w'),ensure_ascii=False,indent=2)
print('GEOMETRY_REVIEW',json.dumps({'geometry_gate_pass':report['geometry_gate_pass'],'integrity':all(x['pass'] for x in master_results),'paving_backing':[(x['paving'],x['unsupported_sample_count']) for x in paving_results],'foundations':[(x['bed'],x['pass']) for x in subresults],'rock':rockresults,'route':report['static_route'],'negative_control':report['negative_control']},ensure_ascii=False))

# Blender may swallow Python SystemExit; exit the process explicitly after flushing on failure.
if not report['geometry_gate_pass']:
 sys.stdout.flush();sys.stderr.flush();os._exit(1)
