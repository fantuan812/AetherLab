"""Bounded south-route landscape pass. Keeps all route meshes and gameplay anchors.
Blender 4.3; open the canonical v1 input, pass --root OUTPUT --manifest INPUT.
Original shared masters, materials, characters and architecture stay untouched.
"""
import bpy,math,json,sys,argparse,hashlib,random,collections
from pathlib import Path
from mathutils import Vector
from mathutils.bvhtree import BVHTree
p=argparse.ArgumentParser();p.add_argument('--root',required=True);p.add_argument('--manifest',required=True);a=p.parse_args(sys.argv[sys.argv.index('--')+1:])
root=Path(a.root).resolve();(root/'source').mkdir(parents=True,exist_ok=True);(root/'docs').mkdir(exist_ok=True)
source=bpy.data.filepath;sha=lambda f:hashlib.sha256(Path(f).read_bytes()).hexdigest();inputsha=sha(source)
if inputsha!='b78c6ed07157f1cd2f972b4391416417445382dfd6bfdaf6a18c940bbd6eef6c':raise RuntimeError('Expected frozen PR35 input')
d=json.loads(Path(a.manifest).read_text());s=bpy.context.scene;rng=random.Random(20261002)
if d.get('source_sha256')!=inputsha:raise RuntimeError('Manifest does not describe the fixed PR35 source')
segments=[]
for rr in d['routes']:
 for p,q in zip(rr['points_m'],rr['points_m'][1:]):segments.append((p,q,rr['width_m'],rr['id']))
def route_distance(x,y):
 best=(1e9,0,None)
 for p,q,w,n in segments:
  dx=q[0]-p[0];dy=q[1]-p[1];l=dx*dx+dy*dy
  if not l:continue
  t=max(0,min(1,((x-p[0])*dx+(y-p[1])*dy)/l));dist=math.hypot(x-p[0]-t*dx,y-p[1]-t*dy)-w/2
  if dist<best[0]:best=(dist,p[2]+t*(q[2]-p[2]),n)
 return best
def smooth(t):t=max(0,min(1,t));return t*t*(3-2*t)
def mask(x,y):
 # Southern strip only. No town/other region terrain or outer world edge edit.
 return smooth((x+165)/30)*smooth((105-x)/30)*smooth((y+397)/18)*smooth((-91-y)/23)
def protected(x,y):return -91<x<-43 and -379<y<-318
terrain=bpy.data.objects['WORLD_Terrain_Continuous_800x800m'];me=terrain.data
original=[v.co.copy() for v in me.vertices];coord={(round(v.x,3),round(v.y,3)):v.z for v in original}
changes=[]
for v in me.vertices:
 x,y,z=v.co;m=mask(x,y)
 if not m or protected(x,y):continue
 dist,grade,n=route_distance(x,y)
 # Keep the old 6m shoulder safety band exact, feather hill detail outside it.
 f=m*smooth((dist-6)/13)
 if not f:continue
 samples=[coord.get((round(x+dx,3),round(y+dy,3)),z) for dx in [-8,-4,0,4,8] for dy in [-8,-4,0,4,8]]
 softened=sum(samples)/len(samples)
 hills=0
 for cx,cy,rx,ry,h in [(-97,-285,22,34,6.8),(-13,-303,22,27,4.7),(-72,-231,28,32,6.5),(18,-213,26,29,7.8),(-50,-157,24,25,3.5),(31,-117,28,23,2.0),(-104,-370,21,22,4.8)]:
  hills+=h*math.exp(-((x-cx)/rx)**2-((y-cy)/ry)**2)
 ripple=.35*math.sin(x*.19+y*.06)*math.sin(y*.13)
 nz=z+f*((softened-z)*.8+hills+ripple)
 v.co.z=nz
 if abs(nz-z)>1e-6:changes.append({'vertex':v.index,'xyz_before':list(original[v.index]),'z_after':nz})
# Smooth normals only within the edited strip; geometry/topology outside stays exact.
for poly in me.polygons:
 if all(mask(me.vertices[i].co.x,me.vertices[i].co.y)>.02 for i in poly.vertices):poly.use_smooth=True
me.update()
terrain['landscape_pass']='South corridor v2: bounded shoulder relief and clustered original foliage; routes fixed'
terrain['production_status']='global_blockout_with_bounded_south_landscape_pass'
# Terrain-only procedural material is copied: the source material IDs are immutable.
base=bpy.data.materials['GB_Terrain_Cool_Moss'];ma=base.copy();ma.name='LAND_South_Moss_Soil_Variation';nt=ma.node_tree
geom=nt.nodes.new('ShaderNodeNewGeometry');noise=nt.nodes.new('ShaderNodeTexNoise');noise.inputs['Scale'].default_value=.095;noise.inputs['Detail'].default_value=2.5;noise.inputs['Roughness'].default_value=.7;nt.links.new(geom.outputs['Position'],noise.inputs['Vector'])
ramp=nt.nodes.new('ShaderNodeValToRGB');ramp.color_ramp.elements[0].position=.18;ramp.color_ramp.elements[0].color=(.095,.137,.113,1);ramp.color_ramp.elements[1].position=.85;ramp.color_ramp.elements[1].color=(.24,.267,.20,1);mid=ramp.color_ramp.elements.new(.51);mid.color=(.157,.202,.157,1);nt.links.new(noise.outputs['Fac'],ramp.inputs['Fac'])
# Per-point fade avoids a rectangle at the boundary of this local material pass.
attr=me.attributes.new('SouthLandscapeBlend','FLOAT','POINT')
for v in me.vertices:attr.data[v.index].value=mask(v.co.x,v.co.y)
att=nt.nodes.new('ShaderNodeAttribute');att.attribute_name='SouthLandscapeBlend';mix=nt.nodes.new('ShaderNodeMixRGB');mix.blend_type='MIX';mix.inputs[1].default_value=(.155,.21,.19,1);nt.links.new(att.outputs['Fac'],mix.inputs[0]);nt.links.new(ramp.outputs['Color'],mix.inputs[2]);nt.links.new(mix.outputs['Color'],nt.nodes.get('Principled BSDF').inputs['Base Color'])
me.materials.append(ma)
for poly in me.polygons:poly.material_index=len(me.materials)-1
# Shared path material preserves geometry while reducing the pale road-strip read.
road=bpy.data.materials['GB_Continuous_Route_Substrate'].copy();road.name='LAND_South_Worn_Route';rn=road.node_tree;nn=rn.nodes.new('ShaderNodeTexNoise');nn.inputs['Scale'].default_value=1.4;nn.inputs['Detail'].default_value=2;gg=rn.nodes.new('ShaderNodeNewGeometry');rn.links.new(gg.outputs['Position'],nn.inputs['Vector']);rr=rn.nodes.new('ShaderNodeValToRGB');rr.color_ramp.elements[0].color=(.20,.24,.21,1);rr.color_ramp.elements[1].color=(.285,.30,.25,1);rn.links.new(nn.outputs['Fac'],rr.inputs['Fac']);rn.links.new(rr.outputs['Color'],rn.nodes.get('Principled BSDF').inputs['Base Color'])
for name in ['ROUTE_C01_Mountain_Main','ROUTE_C01_Mountain_Rescue_Bypass']:
 o=bpy.data.objects[name];o.data.materials[0]=road
# Actual final terrain ray query for every old and new environmental contact.
bvh=BVHTree.FromPolygons([v.co for v in me.vertices],[tuple(p.vertices) for p in me.polygons])
def ground(x,y):
 hit=bvh.ray_cast(Vector((x,y,100)),Vector((0,0,-1)),200)[0]
 if hit is None:raise RuntimeError('Outside ground '+str((x,y)))
 return hit.z
old_moves=[]
for o in bpy.data.collections['08_CONTEXT_FOLIAGE'].objects:
 if o.type!='MESH' or o.get('master') not in {'SRC_Tree_0','SRC_Tree_1','SRC_Shrub','SRC_Fern','SRC_Grass','KIT_Rock_River_A'}:continue
 x,y,z=o.location
 if mask(x,y)>.001 and not protected(x,y):
  nz=ground(x,y)-min(v.co.z for v in o.data.vertices)
  if abs(nz-z)>.001:old_moves.append({'object':o.name,'before':list(o.location),'after':[x,y,nz]});o.location.z=nz
col=bpy.data.collections.new('12_SOUTH_LANDSCAPE_V2');s.collection.children.link(col)
new=[];trees=[]
def add(master,x,y,kind):
 src=bpy.data.objects[master];name='LAND2_'+kind+'_'+str(len(new)).zfill(4);o=bpy.data.objects.new(name,src.data);col.objects.link(o);o.rotation_euler.z=rng.uniform(0,math.tau);o.location=(x,y,ground(x,y)-min(v.co.z for v in src.data.vertices));o['master']=master;o['role']='reused_instance';o['reuse_class']=src.get('reuse_class','unchanged_kit_master');o['landscape_batch']='south_v2';new.append({'object':name,'master':master,'position_m':list(o.location),'kind':kind});return o
def allowed(x,y,margin):
 return mask(x,y)>.3 and not protected(x,y) and route_distance(x,y)[0]>margin
# Alternating thickets frame each bend while an open view fan reveals the town gate.
patches=[(-96,-365,13,18,16),(-36,-366,12,14,12),(-101,-314,15,22,19),(-23,-302,14,21,17),(-84,-271,17,19,19),(2,-260,16,22,17),(-76,-226,16,20,18),(17,-218,18,20,19),(-51,-187,12,13,10),(22,-163,14,18,13),(-39,-135,13,16,10),(27,-110,12,11,8)]
for cx,cy,rx,ry,target in patches:
 accepted=0
 for trial in range(target*40):
  if accepted>=target:break
  ang=rng.uniform(0,math.tau);rad=math.sqrt(rng.random());x=cx+math.cos(ang)*rad*rx;y=cy+math.sin(ang)*rad*ry
  if not allowed(x,y,6) or any(math.hypot(x-xx,y-yy)<4.4 for xx,yy in trees):continue
  add('SRC_Tree_'+str(accepted%2),x,y,'Tree');trees.append((x,y));accepted+=1
 # Understory groups occur at thicket bases, not uniformly over the full map.
 for j in range(target*7):
  ang=rng.uniform(0,math.tau);rad=math.sqrt(rng.random());x=cx+math.cos(ang)*rad*(rx+2);y=cy+math.sin(ang)*rad*(ry+2)
  if allowed(x,y,2.4):add(rng.choice(['SRC_Grass','SRC_Grass','SRC_Fern','SRC_Shrub']),x,y,'Understory')
# Patchy narrow verge rhythm on the long mainline, with holes at functional access.
main=next(r for r in d['routes'] if r['id']=='C01_Mountain_Main')
for p,q in zip(main['points_m'],main['points_m'][1:]):
 dx=q[0]-p[0];dy=q[1]-p[1];L=math.hypot(dx,dy)
 for k in range(max(1,int(L*2.5))):
  t=rng.random();x=p[0]+dx*t;y=p[1]+dy*t
  if y>-99:continue
  wave=math.sin(y*.14)+.45*math.sin(y*.43)
  if wave<-.35 and rng.random()<.85:continue
  side=rng.choice([-1,1]);offset=rng.uniform(4.0,7.7);xx=x-dy/L*side*offset;yy=y+dx/L*side*offset
  if allowed(xx,yy,.75):add(rng.choice(['SRC_Grass','SRC_Grass','SRC_Fern']),xx,yy,'Verge')
for cx,cy,rx,ry,_ in patches[1:9]:
 for j in range(8):
  x=cx+rng.uniform(-rx,rx);y=cy+rng.uniform(-ry,ry)
  if allowed(x,y,3.5):add('KIT_Rock_River_A',x,y,'Rock')
s['landscape_v2_scope']='South spawn-to-town corridor only; six regions/twelve scene centers and all route centerlines unchanged; not UE navigation or final art'
s.cycles.use_denoising=False;bpy.context.view_layer.update()
out=root/'source/AetherLab_Global_World_Blockout_v1.blend';bpy.ops.wm.save_as_mainfile(filepath=str(out),compress=True)
outputsha=sha(out)
d['source_sha256']=outputsha;d['source_size_bytes']=out.stat().st_size;d['instances'] += [{'object':r['object'],'master':r['master']} for r in new];d['stage']='bounded south landscape v2 frozen candidate';d['landscape_v2']={'input_sha256':inputsha,'new_shared_instances':len(new),'new_by_master':dict(collections.Counter(r['master'] for r in new)),'terrain_changed_vertices':len(changes),'old_context_contacts_adjusted':len(old_moves),'source_geometry_scope':'Only original continuous terrain positions/south normals, terrain shader assignment and two south road material slots changed. All old route vertex coordinates and gameplay anchors retained.'}
(root/'docs/World_Manifest.json').write_text(json.dumps(d,ensure_ascii=False,indent=2))
report={'input_source_sha256':inputsha,'output_source_sha256':outputsha,'output_bytes':out.stat().st_size,'terrain_changes':changes,'old_context_vertical_moves':old_moves,'new_instances':new,'new_materials':[ma.name,road.name],'shared_mesh_count':len(set(r['master'] for r in new)),'routes_changed_geometry':[],'gameplay_anchors_moved':[],'ue_compiled_or_tested':False}
(root/'docs/Landscape_Changes.json').write_text(json.dumps(report,ensure_ascii=False,indent=2))
print('FROZEN',outputsha,'new',len(new),'terrain_vertices',len(changes),'old_contact_moves',len(old_moves),flush=True)
