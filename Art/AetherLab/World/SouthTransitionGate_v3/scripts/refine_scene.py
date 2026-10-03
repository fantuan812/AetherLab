"""South road-to-terrain transitions and SCN03 architectural articulation.
Run on frozen 2125fbd4 PR36 source. No route, gameplay anchor or character edits.
"""
import bpy,bmesh,math,json,hashlib,sys,argparse,collections
from pathlib import Path
from mathutils import Vector
from mathutils.bvhtree import BVHTree
p=argparse.ArgumentParser();p.add_argument('--root',required=True);p.add_argument('--manifest',required=True);a=p.parse_args(sys.argv[sys.argv.index('--')+1:]);root=Path(a.root).resolve()
for n in ['source','docs']: (root/n).mkdir(parents=True,exist_ok=True)
sha=lambda f:hashlib.sha256(Path(f).read_bytes()).hexdigest();source=bpy.data.filepath
assert sha(source)=='2125fbd4f5887aea272240581c062b5adbb0ce66c557b71f236dcd82b0c00ca7'
d=json.load(open(a.manifest));routes=[r for r in d['routes'] if r['id'].startswith('C01_Mountain')]
segments=[(Vector(p),Vector(q),r['width_m']/2) for r in routes for p,q in zip(r['points_m'],r['points_m'][1:])]
def nearest(x,y):
 out=[]
 for p,q,w in segments:
  dx,dy=q.x-p.x,q.y-p.y;t=max(0,min(1,((x-p.x)*dx+(y-p.y)*dy)/(dx*dx+dy*dy)));dist=math.hypot(x-p.x-t*dx,y-p.y-t*dy)
  out.append((dist-w,p.z+t*(q.z-p.z),dist))
 return min(out)
def smooth(t):t=max(0,min(1,t));return t*t*(3-2*t)
def weight(x,y):return smooth((y+384)/8)*smooth((-86-y)/8)
terrain=bpy.data.objects['WORLD_Terrain_Continuous_800x800m'];me=terrain.data;nv=len(me.vertices);nf=len(me.polygons)
# Subdivide only local corridor faces. Boundary edges are split coherently in
# BMesh so the continuous terrain remains one connected mesh without T cracks.
bm=bmesh.new();bm.from_mesh(me)
edges=[e for e in bm.edges if all(weight(v.co.x,v.co.y)>0 and nearest(v.co.x,v.co.y)[0]<14 for v in e.verts)]
bmesh.ops.subdivide_edges(bm,edges=edges,cuts=3,use_grid_fill=True);bm.to_mesh(me);bm.free()
# Derive target heights from the actual saved route mesh, including its
# mitered bend boundaries; the centerline alone loses those crossfall details.
roadtrees=[];boundaries=[]
for rr in routes:
 ro=bpy.data.objects['ROUTE_'+rr['id']];rv=[ro.matrix_world@v.co for v in ro.data.vertices]
 roadtrees.append(BVHTree.FromPolygons(rv,[tuple(p.vertices) for p in ro.data.polygons]))
 es=collections.defaultdict(list)
 for f in ro.data.polygons:
  ids=list(f.vertices)
  for i,j in zip(ids,ids[1:]+ids[:1]):es[tuple(sorted([tuple(round(c,5) for c in rv[i]),tuple(round(c,5) for c in rv[j])]))].append((rv[i],rv[j]))
 boundaries += [v[0] for v in es.values() if len(v)==1]
def meshgrade(x,y):
 hits=[b.ray_cast(Vector((x,y,100)),Vector((0,0,-1)),200)[0] for b in roadtrees]
 zs=[h.z for h in hits if h is not None]
 if zs:return min(zs)
 best=(1e9,0)
 for p,q in boundaries:
  dx,dy=q.x-p.x,q.y-p.y;ll=dx*dx+dy*dy
  if ll<1e-10:continue
  t=max(0,min(1,((x-p.x)*dx+(y-p.y)*dy)/ll));dist=(x-p.x-t*dx)**2+(y-p.y-t*dy)**2
  if dist<best[0]:best=(dist,p.z+t*(q.z-p.z))
 return best[1]
old=[v.co.copy() for v in me.vertices];changed=[]
for v in me.vertices:
 x,y,z=v.co;dist,grade,_=nearest(x,y);w=weight(x,y)*(1-smooth((dist-2)/10))
 if w<=0:continue
 target=meshgrade(x,y)-.055-max(0,dist)*.025
 nz=z+(target-z)*w
 if abs(nz-z)>1e-6:v.co.z=nz;changed.append([v.index,z,nz])
for poly in me.polygons:
 if all(weight(me.vertices[i].co.x,me.vertices[i].co.y)>0 and nearest(me.vertices[i].co.x,me.vertices[i].co.y)[0]<14 for i in poly.vertices):poly.use_smooth=True
me.update();terrain['road_transition_v3']='Local coherent terrain subdivision, grade-conforming earth shoulders; route surfaces unchanged'
# Original south point color attribute interpolates during subdivision. Add a
# graded soil shoulder using a second point attribute; no new texture assets.
attr=me.attributes.new('SouthShoulderEarth','FLOAT','POINT')
for v in me.vertices:
 dist,_,_=nearest(v.co.x,v.co.y);attr.data[v.index].value=weight(v.co.x,v.co.y)*(1-smooth((dist-.25)/2.5))*.7
base=bpy.data.materials['LAND_South_Moss_Soil_Variation'];mat=base.copy();mat.name='LAND3_Grade_Conforming_Moss_Earth';nt=mat.node_tree;bs=nt.nodes.get('Principled BSDF');oldout=bs.inputs['Base Color'].links[0].from_socket
at=nt.nodes.new('ShaderNodeAttribute');at.attribute_name='SouthShoulderEarth';mix=nt.nodes.new('ShaderNodeMixRGB');mix.inputs[2].default_value=(.185,.205,.164,1);nt.links.new(oldout,mix.inputs[1]);nt.links.new(at.outputs['Fac'],mix.inputs[0]);nt.links.new(mix.outputs[0],bs.inputs['Base Color']);me.materials.append(mat)
for poly in me.polygons:poly.material_index=len(me.materials)-1
# Contact correction for existing foliage only, using actual transformed mesh
# bottom. Retain XY, rotations, scales and shared mother meshes.
bvh=BVHTree.FromPolygons([v.co for v in me.vertices],[tuple(p.vertices) for p in me.polygons]);moved=[]
for c in ['08_CONTEXT_FOLIAGE','12_SOUTH_LANDSCAPE_V2']:
 for o in bpy.data.collections[c].objects:
  if o.type!='MESH' or not o.get('master'):continue
  x,y,z=o.location;dist,_,_=nearest(x,y)
  if not weight(x,y) or dist>=12:continue
  hit=bvh.ray_cast(Vector((x,y,100)),Vector((0,0,-1)),200)[0]
  if hit is None:raise RuntimeError('No terrain at '+o.name)
  minz=min((o.matrix_world@v.co).z-z for v in o.data.vertices);nz=hit.z-minz
  if abs(nz-z)>.0001:o.location.z=nz;moved.append({'object':o.name,'before_z':z,'after_z':nz})
# A true geometric earth verge bridges the final few centimeters at the saved
# road boundary. Derive each closed boundary loop from welded road-face edges;
# miter corner offsets, then conform its outer ring to the actual terrain.
verges=bpy.data.collections.new('14_SOUTH_ROAD_EARTH_VERGES_V3');bpy.context.scene.collection.children.link(verges)
# Subtract the two target mountain road projections from the verges, especially the
# main/bypass junction. Convex half-plane difference preserves original roads
# and prevents a shoulder from becoming a raised sheet over the other mountain route.
road_polys=[];road_grid=collections.defaultdict(set)
for ro in bpy.data.objects:
 if ro.type!='MESH' or not ro.name.startswith('ROUTE_C01_Mountain'):continue
 for f in ro.data.polygons:
  ps=[ro.matrix_world@ro.data.vertices[i].co for i in f.vertices]
  if min(v.y for v in ps)>-90 or max(v.y for v in ps)<-380:continue
  area=sum(p.x*q.y-q.x*p.y for p,q in zip(ps,ps[1:]+ps[:1]))
  if area<0:ps.reverse()
  ii=len(road_polys);road_polys.append(ps)
  for gx in range(math.floor(min(v.x for v in ps)/8),math.floor(max(v.x for v in ps)/8)+1):
   for gy in range(math.floor(min(v.y for v in ps)/8),math.floor(max(v.y for v in ps)/8)+1):road_grid[(gx,gy)].add(ii)
def halfclip(poly,a,b,inside):
 out=[]
 for p,q in zip(poly,poly[1:]+poly[:1]):
  dp=(b.x-a.x)*(p.y-a.y)-(b.y-a.y)*(p.x-a.x);dq=(b.x-a.x)*(q.y-a.y)-(b.y-a.y)*(q.x-a.x)
  pin=dp>=-1e-8 if inside else dp<=1e-8;qin=dq>=-1e-8 if inside else dq<=1e-8
  if pin:out.append(p)
  if pin!=qin and abs(dp-dq)>1e-12:out.append(p.lerp(q,dp/(dp-dq)))
 return out


def subtract(poly,clip):
 pieces=[];remaining=poly
 for a,b in zip(clip,clip[1:]+clip[:1]):
  outside=halfclip(remaining,a,b,False)
  if len(outside)>=3:pieces.append(outside)
  remaining=halfclip(remaining,a,b,True)
  if len(remaining)<3:break
 return pieces
verge_counts=[]
for rr in routes:
 ro=bpy.data.objects['ROUTE_'+rr['id']];ed=collections.defaultdict(list)
 for f in ro.data.polygons:
  vs=[ro.matrix_world@ro.data.vertices[i].co for i in f.vertices];center=sum(vs,Vector())/len(vs)
  for p,q in zip(vs,vs[1:]+vs[:1]):
   k=tuple(sorted([tuple(round(c,5) for c in p),tuple(round(c,5) for c in q)]));ed[k].append((p,q,center))
 boundaries=[r[0] for r in ed.values() if len(r)==1];norms=collections.defaultdict(list)
 for p,q,c in boundaries:
  e=q-p;n=Vector((-e.y,e.x,0)).normalized()
  if n.dot((p+q)*.5-c)<0:n=-n
  for v in [p,q]:norms[tuple(round(x,5) for x in v)].append(n)
 def offset(v):
  ns=norms[tuple(round(x,5) for x in v)];n=sum(ns,Vector()).normalized();return n/max(.5,n.dot(ns[0]))
 verts=[];faces=[]
 for p,q,c in boundaries:
  # Do not extend into unchanged town or beyond the spawn approach.
  if max(p.y,q.y)>-96 or min(p.y,q.y)<-372:continue
  np,nq=offset(p),offset(q);N=max(1,math.ceil((q-p).length/.5));base=len(verts)
  for i in range(N+1):
   tt=i/N;v=p.lerp(q,tt);n=np.lerp(nq,tt)
   for u in [0,.2,.65,1]:
    xy=v+n*(1.5*u);hit=bvh.ray_cast(Vector((xy.x,xy.y,100)),Vector((0,0,-1)),200)[0]
    if hit is None:raise RuntimeError('Verge outside terrain')
    # Outer ring is a 4mm terrain overlay to avoid z-fighting; not welded.
    z=v.z-.012+(hit.z+.004-(v.z-.012))*u
    if u>0:z=max(z,hit.z+.004)
    verts.append((xy.x,xy.y,z))
  for i in range(N):
   for j in range(3):
    k=base+i*4+j;faces.append((k,k+4,k+5,k+1))
 cv=[];cf=[]
 for face in faces:
  poly=[Vector(verts[i]) for i in face];ids=set()
  for gx in range(math.floor(min(v.x for v in poly)/8),math.floor(max(v.x for v in poly)/8)+1):
   for gy in range(math.floor(min(v.y for v in poly)/8),math.floor(max(v.y for v in poly)/8)+1):ids.update(road_grid[(gx,gy)])
  pieces=[poly]
  for ii in sorted(ids):
   pieces=[p for piece in pieces for p in subtract(piece,road_polys[ii])]
   if not pieces:break
  for piece in pieces:
   piece=[v.copy() for v in piece]
   area=abs(sum(p.x*q.y-q.x*p.y for p,q in zip(piece,piece[1:]+piece[:1])))/2
   if area<1e-5:continue
   # Tie newly cut vertices to the neighbouring road edge, with a 0.5m
   # height fade. Cropping alone would leave a raised lip at a crossing.
   for v in piece:
    near=(1e9,0)
    for ii in ids:
     ps=road_polys[ii]
     for aa,bb in zip(ps,ps[1:]+ps[:1]):
      dx,dy=bb.x-aa.x,bb.y-aa.y;ll=dx*dx+dy*dy
      if ll<1e-12:continue
      tt=max(0,min(1,((v.x-aa.x)*dx+(v.y-aa.y)*dy)/ll));dd=math.hypot(v.x-aa.x-tt*dx,v.y-aa.y-tt*dy)
      if dd<near[0]:near=(dd,aa.z+tt*(bb.z-aa.z))
    if near[0]<.5 and abs(near[1]-v.z)<.3:v.z+=(near[1]-.012-v.z)*(1-smooth(near[0]/.5))
   base=len(cv);cv.extend([tuple(v) for v in piece]);cf.append(tuple(range(base,base+len(piece))))
 vm=bpy.data.meshes.new('LAND3_EarthVerge_'+rr['id']);vm.from_pydata(cv,[],cf);vm.materials.append(mat)
 # Weld matching segment vertices; clipped shoulders may contain separate
 # terrain-supported patches.
 bm=bmesh.new();bm.from_mesh(vm);bmesh.ops.remove_doubles(bm,verts=list(bm.verts),dist=.00001)
 bmesh.ops.triangulate(bm,faces=list(bm.faces),quad_method='BEAUTY',ngon_method='EAR_CLIP')
 # Clipping can create sub-pixel slivers on exactly coincident edges. Remove
 # those degenerate/duplicate faces after welding, then discard unused data.
 for _ in range(2):
  bad=[];seen=set()
  for f in bm.faces:
   key=frozenset(f.verts)
   if f.calc_area()<1e-5 or key in seen:bad.append(f)
   else:seen.add(key)
  if bad:bmesh.ops.delete(bm,geom=bad,context='FACES_ONLY')
  loose_edges=[e for e in bm.edges if not e.link_faces]
  if loose_edges:bmesh.ops.delete(bm,geom=loose_edges,context='EDGES')
  loose_verts=[v for v in bm.verts if not v.link_faces]
  if loose_verts:bmesh.ops.delete(bm,geom=loose_verts,context='VERTS')
 bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces));bm.to_mesh(vm);bm.free()
 for f in vm.polygons:f.use_smooth=True
 # Same world-space material and point fades as the receiving terrain, so
 # geometric repair never turns into two conspicuous dark painted bands.
 av=vm.attributes.new('SouthLandscapeBlend','FLOAT','POINT');ae=vm.attributes.new('SouthShoulderEarth','FLOAT','POINT')
 for v in vm.vertices:
  x,y,z=v.co;av.data[v.index].value=smooth((x+165)/30)*smooth((105-x)/30)*smooth((y+397)/18)*smooth((-91-y)/23)
  dist,_,_=nearest(x,y);ae.data[v.index].value=weight(x,y)*(1-smooth((dist-.25)/2.5))*.7
 vo=bpy.data.objects.new(vm.name,vm);verges.objects.link(vo);vo['role']='bank';vo['source_route']=ro.name;vo['refinement_batch']='south_transition_gate_v3';verge_counts.append({'object':vo.name,'vertices':len(vm.vertices),'faces':len(vm.polygons),'width_m':1.5})
col=bpy.data.collections.new('13_SCN03_GATE_REFINEMENT_V3');bpy.context.scene.collection.children.link(col)
masters=bpy.data.collections.new('92_SCN03_DETAIL_MASTERS__HIDDEN');bpy.context.scene.collection.children.link(masters);masters.hide_render=True;masters.hide_viewport=True
new=[];master_list=[]
# New bounded rectangular trim candidates reuse approved material families.
def boxmaster(name,dim,ma,bevel=.02):
 bpy.ops.mesh.primitive_cube_add();o=bpy.context.object;o.name=name;o.dimensions=dim;bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
 for c in list(o.users_collection):c.objects.unlink(o)
 masters.objects.link(o);o.data.materials.append(bpy.data.materials[ma]);
 if bevel:
  mod=o.modifiers.new('Small manufactured edge','BEVEL');mod.width=bevel;mod.segments=2;bpy.context.view_layer.objects.active=o;bpy.ops.object.modifier_apply(modifier=mod.name)
 o['role']='bounded_gate_trim_candidate';master_list.append(name);return o

def instance(master,name,pos,rot=0):
 src=bpy.data.objects[master];o=bpy.data.objects.new(name,src.data);col.objects.link(o);o.location=pos;o.rotation_euler.z=rot;o['master']=master;o['role']='reused_instance';o['refinement_batch']='south_transition_gate_v3';new.append({'object':name,'master':master});return o
stone=['H01_Damp_stone_0','H01_Damp_stone_1','H01_Damp_stone_2','H01_Damp_stone_3']
for i,ma in enumerate(stone):boxmaster('GATE3_StoneCourse_'+str(i),(.72,.16,.39),ma,.025)
boxmaster('GATE3_StoneEnd',(.34,.16,.39),stone[1],.02)
boxmaster('GATE3_Plinth',(.96,.24,.24),stone[2],.025)
boxmaster('GATE3_Timber_3m',(3,.22,.24),'H01_Weathered_warm_timber',.025)
boxmaster('GATE3_Timber_1m',(1,.20,.20),'H01_Weathered_warm_timber',.02)
# Core material changes are isolated object-mesh copies; original mother and
# materials stay immutable. Original 5x6m opening is never narrowed.
for n in ['SCN03_Gate_Pier-4','SCN03_Gate_Pier4','SCN03_Gate_Lintel']:
 o=bpy.data.objects[n];o.data=o.data.copy();o.data.materials.clear();o.data.materials.append(bpy.data.materials['H01_Base_mortar'])
for side in [-1,1]:
 for y in [-81.86,-78.14]:
  for row in range(14):
   z=.28+row*.414
   if row%2==0:
    xs=[2.90,3.66,4.42,5.18]
    for k,x in enumerate(xs):instance('GATE3_StoneCourse_'+str((row+k)%4),f'GATE3_Face_{side}_{y}_{row}_{k}',(side*x,y,z))
   else:
    for k,x in enumerate([2.71,5.37]):instance('GATE3_StoneEnd',f'GATE3_Half_{side}_{y}_{row}_{k}',(side*x,y,z))
    for k,x in enumerate([3.28,4.04,4.8]):instance('GATE3_StoneCourse_'+str((row+k+1)%4),f'GATE3_Offset_{side}_{y}_{row}_{k}',(side*x,y,z))
  for k in range(3):instance('GATE3_Plinth',f'GATE3_Base_{side}_{y}_{k}',(side*(3.0+k),y,.14))
 # Outside return faces (leave opening at x +/-2.5 untouched).
 for row in range(14):
  for k in range(4):instance('GATE3_StoneCourse_'+str((row+k+side)%4),f'GATE3_Return_{side}_{row}_{k}',(side*5.57,-81.14+k*.76,.28+row*.414),math.pi/2)
# Lintel stone courses remain wholly above 6.0m.
for y in [-81.86,-78.14]:
 for row in range(2):
  for k in range(14):instance('GATE3_StoneCourse_'+str((row+k)%4),f'GATE3_Lintel_{y}_{row}_{k}',(-4.94+k*.76,y,6.22+row*.414))
 for k in range(4):instance('GATE3_Timber_3m',f'GATE3_Fascia_{y}_{k}',(-4.5+k*3,-80+(-2.67 if y<-80 else 2.67),6.96))
# Reorient only the SCN03 roof ridge along the gate span, retaining its exact
# world bounds. This removes the old broad blank front triangle and makes
# the canopy a legible shallow tiled gate roof; gameplay opening is unchanged.
roofcore=bpy.data.objects['SCN03_Gate_Roof'];rm=bpy.data.meshes.new('GATE3_SpanAligned_Roof')
rm.from_pydata([(-6.2,-2.7,0),(6.2,-2.7,0),(6.2,2.7,0),(-6.2,2.7,0),(-6.2,0,1.5),(6.2,0,1.5)],[],[(0,1,5,4),(4,5,2,3),(0,4,3),(1,2,5),(0,3,2,1)])
rm.materials.append(bpy.data.materials['GB_Roof_Slate']);roofcore.data=rm
# A weathered tiled roof from the retained slate family. One shared tile, placed
# on the existing gable planes, with a small overlap and no arbitrary scaling.
boxmaster('GATE3_SlateTile',(.48,.55,.045),'H01_Weathered_slate_2',.012)
for side in [-1,1]:
 for row in range(6):
  dy=(row+.45)*.45;z=8.5-dy*(1.5/2.7)+.05
  for k in range(25):
   x=-6+k*.50+(row%2)*.10
   o=instance('GATE3_SlateTile',f'GATE3_Tile_{side}_{row}_{k}',(x,-80+side*dy,z));o.rotation_euler.x=-side*math.atan(1.5/2.7)
# Register canopy uses original authored weathered roof at exact source scale.
canopy=bpy.data.objects['SCN03_Registration_Canopy'];canopy.hide_render=True;canopy.hide_viewport=True
roof=instance('SRC_Roof_H03','GATE3_Registration_AuthoredRoof',(-10,-72,0));roof.location.z=3.4-min(v.co.z for v in roof.data.vertices)
for y in [-74.6,-69.4]:
 for k in range(2):instance('GATE3_Timber_3m',f'GATE3_CanopyBeam_{y}_{k}',(-11.5+k*3,y,3.26))
# Two existing lantern-post mother instances provide gate scale and warm focus.
for side in [-1,1]:instance('KIT_LanternPost',f'GATE3_EntryLantern_{side}',(side*6.7,-83,0))
bpy.context.view_layer.update();bpy.context.scene['refinement_v3_scope']='Only south road shoulder terrain and SCN03 gate/canopy detail. Full layout retained. UE not run.'
out=root/'source/AetherLab_Global_World_Blockout_v1.blend';bpy.ops.wm.save_as_mainfile(filepath=str(out),compress=True)
report={'input_source_sha256':sha(source),'output_source_sha256':sha(out),'terrain_before_vertices':nv,'terrain_before_polygons':nf,'terrain_after_vertices':len(me.vertices),'terrain_after_polygons':len(me.polygons),'terrain_changed_z_count':len(changed),'terrain_changes':changed,'foliage_contacts':moved,'new_instances':new,'new_trim_masters':master_list,'road_earth_verges':verge_counts,'route_geometry_changed':False,'character_changed':False,'ue_compiled_or_tested':False}
(root/'docs/Transition_Gate_Changes.json').write_text(json.dumps(report,indent=2));d['source_sha256']=sha(out);d['source_size_bytes']=out.stat().st_size;d['stage']='south transition and SCN03 gate refinement v3 candidate';d['instances']+=new;d['instance_count']=len(d['instances']);d['refinement_v3']={k:v for k,v in report.items() if k not in ['terrain_changes','foliage_contacts','new_instances']};(root/'docs/World_Manifest.json').write_text(json.dumps(d,indent=2));print('FROZEN',sha(out),'terrain',len(changed),'new',len(new),'contacts',len(moved),flush=True)
