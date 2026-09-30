from pathlib import Path
import bpy, math, random, os, json
from mathutils import Vector
random.seed(81)
ROOT = str(Path(__file__).resolve().parents[1])
bpy.ops.object.select_all(action='SELECT'); bpy.ops.object.delete(use_global=False)
for c in list(bpy.data.collections):
 if c.name!='Collection': bpy.data.collections.remove(c)
base=bpy.data.collections['Collection']; base.name='SCN_01_Rain_Mountain_Path'
cols={}
def col(n):
 c=bpy.data.collections.new(n); bpy.context.scene.collection.children.link(c); cols[n]=c; return c
for n in ['SM_RestStop','SM_BrokenCart','SM_WaterBarrel','SM_Path_4m','SM_Wall_4m','SM_Fence_4m','SM_LanternPost','SM_SouthGate','ENV_Terrain','ENV_Rocks','ENV_Foliage','ENV_PathExtensions','FX_ShelteredFire','COLLISION','LIGHTS']: col(n)
current='ENV_Terrain'
def put(o,n,m=None):
 o.name=n
 for c in list(o.users_collection): c.objects.unlink(o)
 cols[current].objects.link(o)
 if m:o.data.materials.append(m)
 return o
def mat(n,color,rough=.6,metal=0,noise=0):
 m=bpy.data.materials.new(n); m.diffuse_color=(*color,1); m.use_nodes=True; nt=m.node_tree; p=nt.nodes.get('Principled BSDF'); p.inputs['Base Color'].default_value=(*color,1); p.inputs['Roughness'].default_value=rough;p.inputs['Metallic'].default_value=metal
 if noise:
  t=nt.nodes.new('ShaderNodeTexNoise'); t.inputs['Scale'].default_value=noise;t.inputs['Detail'].default_value=3
  ramp=nt.nodes.new('ShaderNodeValToRGB'); ramp.color_ramp.elements[0].color=(*(x*.55 for x in color),1);ramp.color_ramp.elements[1].color=(*(min(x*1.3,1) for x in color),1);nt.links.new(t.outputs['Fac'],ramp.inputs[0]);nt.links.new(ramp.outputs[0],p.inputs['Base Color'])
  bump=nt.nodes.new('ShaderNodeBump');bump.inputs['Strength'].default_value=.23;bump.inputs['Distance'].default_value=.08;nt.links.new(t.outputs['Fac'],bump.inputs['Height']);nt.links.new(bump.outputs[0],p.inputs['Normal'])
 return m
stone=[mat('Wet basalt '+str(i),(.18+i*.025,.24+i*.025,.27+i*.025),.25,0,4) for i in range(4)]
wood=mat('Aged cedar',(.19,.09,.035),.6,0,5); woodlight=mat('Split cedar edges',(.32,.18,.07),.65,0,7)
roofm=[mat('Glazed slate '+str(i),(.045+i*.016,.12+i*.022,.14+i*.02),.28,.08,8) for i in range(4)]
moss=mat('Rain moss',(.16,.27,.08),.85,0,8);earth=mat('Wet mountain earth',(.085,.13,.105),.8,0,3);rock=mat('Mountain cliff',(.14,.21,.24),.8,0,2)
leafm=[mat('Jade leaves '+str(i),(.025+i*.025,.19+i*.035,.125+i*.01),.7) for i in range(4)]
metal=mat('Forged iron',(.055,.075,.08),.3,.8);brass=mat('Lantern aged brass',(.43,.27,.07),.28,.65);water=mat('Rainwater',(.085,.2,.24),.06,.35);cloth=mat('Linen shade',(.49,.40,.26),.9);flag=mat('Gate indigo banners',(.025,.10,.17),.85)
def emit(n,c,s):
 m=mat(n,c);p=m.node_tree.nodes.get('Principled BSDF');p.inputs['Emission Color'].default_value=(*c,1);p.inputs['Emission Strength'].default_value=s;return m
warm=emit('Amber lantern glass',(1,.42,.08),4); flame=emit('Fire gold',(1,.16,.015),6);core=emit('Flame heart',(1,.64,.12),8)
def cube(n,loc,scale,m,bev=.04):
 bpy.ops.mesh.primitive_cube_add(size=1,location=loc);o=put(bpy.context.object,n,m);o.dimensions=scale;bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
 if bev: mod=o.modifiers.new('Hand softened edges','BEVEL');mod.width=bev;mod.segments=2;o.modifiers.new('Weighted normals','WEIGHTED_NORMAL')
 return o
def cyl(n,loc,r,depth,m,vertices=16,rot=None):
 bpy.ops.mesh.primitive_cylinder_add(vertices=vertices,radius=r,depth=depth,location=loc);o=put(bpy.context.object,n,m)
 if rot:o.rotation_euler=rot
 mod=o.modifiers.new('Edge wear','BEVEL');mod.width=.025;mod.segments=2;o.modifiers.new('Weighted normals','WEIGHTED_NORMAL');return o
def ico(n,loc,scale,m,sub=1):
 bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=sub,radius=1,location=loc);o=put(bpy.context.object,n,m);o.scale=scale;return o
def beam(n,a,b,w,m):
 d=Vector(b)-Vector(a);o=cube(n,(Vector(a)+Vector(b))/2,(w,w,d.length),m,.025);o.rotation_euler=d.to_track_quat('Z','Y').to_euler();return o
def torus(n,loc,major,minor,m,rot=None):
 bpy.ops.mesh.primitive_torus_add(major_segments=24,minor_segments=6,location=loc,major_radius=major,minor_radius=minor);o=put(bpy.context.object,n,m)
 if rot:o.rotation_euler=rot
 return o
def point(n,loc,energy,color,size=.5):
 data=bpy.data.lights.new(n,'POINT');data.energy=energy;data.color=color;data.shadow_soft_size=size;o=bpy.data.objects.new(n,data);cols['LIGHTS'].objects.link(o);o.location=loc
# Elevated cutaway terrain: rain-soaked pass and lower bypass
current='ENV_Terrain'
cube('Main mountain shelf',(0,5,-1.15),(14,28,2.0),earth,.7)
cube('Lower bypass terrace',(8,4,-2.0),(5,27,1.8),earth,.7)
for i in range(48):
 x=random.choice([-1,1])*random.uniform(5.8,7.5);y=random.uniform(-8,19)
 ico('Layered mountain rock',(x,y,-.9),(random.uniform(.7,1.8),random.uniform(.7,1.8),random.uniform(.7,1.4)),rock,1)
# Paving uses separated irregular quarried flagstones
for row in range(23):
 current='SM_Path_4m' if row<4 else 'ENV_PathExtensions'
 for j in range(4):
  x=-1.75+j*1.05+random.uniform(-.09,.09);y=-7+row*1.12
  o=cube('Flagstone_%02d_%d'%(row,j),(x,y,.03+random.uniform(-.04,.04)),(.96+random.uniform(-.09,.05),1.03+random.uniform(-.08,.07),.20),random.choice(stone),.10);o.rotation_euler.z=random.uniform(-.08,.08)
 for j in range(2):
  ico('Pathside moss',(random.choice([-2.4,2.5])+random.uniform(-.2,.2),y,.05),(.4,.6,.12),moss)
current='ENV_PathExtensions'
for i in range(23):
 x=8+.75*math.sin(i*.23);y=-8+i*1.1
 for j in range(2):cube('Bypass stepping stone',(x+j*.7-.35,y,-.99),(.64,.9,.13),random.choice(stone),.09)
# Rain puddles inset among the slabs
for i in range(22):
 x=random.uniform(-1.8,1.6);y=random.uniform(-7,17)
 ico('Shallow rain puddle',(x,y,.15),(random.uniform(.18,.46),random.uniform(.3,.7),.009),water,2)
# modular stone wall by pass
for side in [-1,1]:
 for seg in range(6):
  current='SM_Wall_4m' if side==1 and seg==0 else 'ENV_PathExtensions'
  y=-6+seg*4
  if side==-1 and seg<3:continue
  x=3.2*side
  for h in range(2):
   for b in range(4):cube('Rubble parapet block',(x,y+b*.96,h*.4+.25),(.65,.90,.37),random.choice(stone),.075)
  cube('Pillar base',(x,y-.25,.60),(.88,.88,1.2),stone[1],.09);cube('Pillar cap',(x,y-.25,1.24),(1.0,1.0,.18),stone[2],.04)
  for k in range(3):ico('Parapet moss',(x+random.uniform(-.3,.3),y+random.uniform(0,3),.83),(.35,.4,.08),moss)
# Bypass fence
for seg in range(6):
 current='SM_Fence_4m' if seg==0 else 'ENV_PathExtensions';y=-8+seg*4
 for yy in [y,y+2,y+4]:cube('Fence stake',(10.4,yy,-.38),(.17,.17,1.3),wood,.03)
 for z in [-.5,-.1]:beam('Fence rail',(10.4,y,z),(10.4,y+4,z),.13,wood)
# Roofed roadside pavilion
current='SM_RestStop'; cx=-4.7;cy=0.0
cube('Pavilion foundation',(cx,cy,.14),(5.2,5.5,.35),stone[1],.10)
for y in [-2.4,2.4]:
 for x in [cx-2.15,cx+2.15]:
  cube('Stone post foot',(x,y,.43),(.6,.6,.35),stone[2],.04);cube('Cedar post',(x,y,2.0),(.28,.3,3.2),wood,.05)
  for dy in [-1,1]:beam('Diagonal knee brace',(x,y,2.65),(x,y+dy*.75,3.45),.15,wood)
for x in [cx-2.15,cx+2.15]:beam('Pavilion eave beam',(x,-2.95,3.5),(x,2.95,3.5),.28,wood)
for y in [-2.55,0,2.55]:
 beam('Gable tie',(cx-2.5,y,3.6),(cx+2.5,y,3.6),.23,wood)
 beam('Rafter L',(cx-2.8,y,3.65),(cx,y,5.05),.18,wood)
 beam('Rafter R',(cx,y,5.05),(cx+2.8,y,3.65),.18,wood)
beam('Ridge beam',(cx,-3.1,5.08),(cx,3.1,5.08),.24,wood)
# overlapping individually modeled clay/slate tiles, upswept at eaves
for side in [-1,1]:
 for r in range(9):
  u=(r+.5)/9;xx=cx+side*u*2.9;zz=5.12-1.65*u+.24*u**5
  for j in range(18):
   yy=-3.0+j*.35+(r%2)*.06
   o=cube('Overlapping glazed tile',(xx,yy,zz),(.4,.39,.10),random.choice(roofm),.035);o.rotation_euler.y=side*.44
   if random.random()<.065:ico('Roof moss clump',(xx,yy,zz+.10),(.18,.20,.055),moss)
for yy in [-3.18,3.18]:
 for side in [-1,1]:
  beam('Carved gable edge',(cx,yy,5.2),(cx+side*2.55,yy,3.95),.16,woodlight)
  beam('Upturned roof finial',(cx+side*2.55,yy,3.95),(cx+side*3.05,yy,4.0),.16,woodlight)
for i in range(18):cyl('Ridge cap',(cx,-3.0+i*.36,5.23),.16,.4,roofm[2],12,(math.pi/2,0,0))
# rear slatted wall and furniture
for i in range(14):cube('Rear wall board',(cx-2.15,-2.15+i*.33,1.45),(.12,.29,2.0),woodlight,.025)
for y in [-1.45,1.45]:
 cube('Bench seat',(cx-.65,y,.88),(2.5,.4,.16),woodlight)
 for xx in [cx-1.6,cx+.3]:cube('Bench leg',(xx,y,.57),(.15,.3,.6),wood)
cube('Tea table',(cx-1.0,.1,1.23),(1.25,.95,.15),woodlight)
for xx in [cx-1.5,cx-.5]:
 for yy in [-.25,.45]:cube('Table leg',(xx,yy,.76),(.12,.12,.9),wood)
for xx in [cx-1.3,cx-.9]:cyl('Tea cup',(xx,.1,1.4),.08,.18,stone[2])
# lantern reusable mesh
def lantern(x,y,z):
 beam('Lantern hook',(x,y,z+.65),(x,y,z+.35),.055,metal)
 cube('Lantern glowing paper',(x,y,z),(.25,.25,.4),warm,.025)
 for dx in [-.16,.16]:
  for dy in [-.16,.16]:beam('Lantern cage',(x+dx,y+dy,z-.26),(x+dx,y+dy,z+.26),.035,brass)
 for dz in [-.26,.26]:cube('Lantern cap',(x,y,z+dz),(.40,.40,.07),brass,.04)
 point('Lantern warm pool',(x,y,z),75,(1,.48,.16),.45)
lantern(cx+1.65,-1.6,2.6)
for y in [5.0,12.0]:
 current='SM_LanternPost' if y==5 else 'ENV_PathExtensions';x=2.55
 beam('Road lantern post',(x,y,.15),(x,y,2.8),.16,wood);beam('Road lantern arm',(x,y,2.7),(x-.7,y,2.7),.14,wood);lantern(x-.60,y,2.15)
# sheltered iron brazier
current='FX_ShelteredFire';fx=cx+.9;fy=.2
cyl('Brazier bowl',(fx,fy,.78),.47,.23,metal,12)
for a in range(4):
 t=a*math.pi/2;beam('Brazier leg',(fx+math.cos(t)*.35,fy+math.sin(t)*.35,.22),(fx+math.cos(t)*.28,fy+math.sin(t)*.28,.74),.09,metal)
for i in range(4):
 o=cyl('Burning log',(fx+random.uniform(-.15,.15),fy+random.uniform(-.15,.15),.91),.085,.65,wood,8,(math.pi/2,0,i*.7))
for i in range(7):
 x=fx+random.uniform(-.23,.23);y=fy+random.uniform(-.23,.23);h=random.uniform(.35,.7)
 bpy.ops.mesh.primitive_cone_add(vertices=7,radius1=.14,radius2=0,depth=h,location=(x,y,1.0+h/2));o=put(bpy.context.object,'Stylized flame',flame);o.rotation_euler.y=random.uniform(-.3,.3)
 ico('Golden ember',(x,y,1.0),(.11,.10,.15),core)
point('Sheltered fire bounce',(fx,fy,1.2),220,(1,.24,.045),.7)
# pushable water barrel, stave mesh and watertight lid optional
current='SM_WaterBarrel'; bx=-3.0;by=-3.5
for i in range(18):
 a=2*math.pi*i/18;da=math.pi/18*.94;vs=[]
 for z,r in [(0,.46),(.15,.52),(.65,.59),(1.15,.52),(1.30,.46)]:
  for aa in [a-da,a+da]:vs.append((bx+r*math.cos(aa),by+r*math.sin(aa),.23+z))
 faces=[]
 for j in range(4):faces.append((2*j,2*j+1,2*j+3,2*j+2))
 mesh=bpy.data.meshes.new('Curved barrel stave');mesh.from_pydata(vs,[],faces);mesh.update();o=bpy.data.objects.new('Curved oak stave',mesh);cols[current].objects.link(o);o.data.materials.append(woodlight if i%3==0 else wood);mod=o.modifiers.new('Stave thickness','SOLIDIFY');mod.thickness=.05
for z,r in [(.37,.52),(.61,.58),(1.2,.56),(1.47,.48)]:torus('Iron barrel hoop',(bx,by,z),r,.038,metal)
cyl('Contained water',(bx,by,1.40),.43,.02,water,32)
# cart with broken wheel and detachable wheel lying nearby
current='SM_BrokenCart'; qx=-1.8;qy=2.9
for i in range(6):cube('Cart bed plank',(qx-.65+i*.26,qy,.64),(.23,2.0,.14),woodlight)
for x in [qx-.78,qx+.78]:
 for z in [.86,1.13,1.40]:cube('Cart side plank',(x,qy,z),(.12,2.12,.23),wood,.035)
 for y in [qy-.86,qy+.86]:cube('Cart side upright',(x,y,1.17),(.18,.16,1.1),woodlight)
for z in [.86,1.13,1.40]:cube('Cart rear board',(qx,qy+.99,z),(1.6,.13,.23),wood)
beam('Iron axle',(qx-1.08,qy,.53),(qx+1.08,qy,.53),.16,metal)
for xx in [qx-.6,qx+.6]:beam('Pull shaft',(xx,qy-.5,.6),(xx,qy-3.0,.3),.11,woodlight)
def wheel(x,y,z,broken=False,lay=False):
 rot=(0,math.pi/2,0) if not lay else (0,0,0)
 if not broken:
  torus('Wheel wooden rim',(x,y,z),.61,.07,woodlight,rot);torus('Wheel iron tire',(x,y,z),.67,.025,metal,rot)
 else:
  # incomplete curved rim segments make damage legible
  for i in range(9):
   a=i*math.pi/6;b=a+math.pi/6
   beam('Broken rim segment',(x,y+math.cos(a)*.61,z+math.sin(a)*.61),(x,y+math.cos(b)*.61,z+math.sin(b)*.61),.09,woodlight)
 cyl('Wheel hub',(x,y,z),.13,.22,metal,12,rot)
 for i in range(10 if not broken else 6):
  a=i*2*math.pi/10
  b=(x+math.cos(a)*.58,y+math.sin(a)*.58,z) if lay else (x,y+math.cos(a)*.58,z+math.sin(a)*.58)
  beam('Wheel spoke',(x,y,z),b,.05,woodlight)
wheel(qx-.94,qy,.60);wheel(qx+.94,qy,.60,True);wheel(qx+1.5,qy-1.2,.27,False,True)
for i in range(4):
 o=cube('Splintered cart debris',(qx+random.uniform(.7,1.8),qy+random.uniform(-1.8,.5),.22),(.12,.7,.07),woodlight,.01);o.rotation_euler.z=random.uniform(-1,1)
# Distant south gate, true open arched portal assembled from radial stones
current='SM_SouthGate'; gy=19.2
for side in [-1,1]:
 gx=side*3.7
 cube('Gate curtain',(side*5.0,gy,2.3),(5.5,1.5,4.6),stone[1],.12)
 cube('Gate tower',(gx,gy-.1,3.0),(2.4,2.5,6.0),stone[0],.12)
 for level in range(9):
  for k in range(3):cube('Tower facing ashlar',(gx-1+k*.98,gy-1.39,.35+level*.59),(.91,.12,.50),stone[(k+level)%4],.035)
 for k in range(4):cube('Crenel merlon',(gx-1.1+k*.73,gy-1.45,6.15),(.5,.6,.6),stone[2],.03)
 # watch pavilion above towers
 for dx in [-.78,.78]:
  for dy in [-.72,.72]:cube('Watchtower timber',(gx+dx,gy+dy,6.85),(.16,.16,1.4),wood)
 cube('Watchtower lit window',(gx,gy,6.85),(1.1,1.1,.65),warm)
 # tiered pyramidal Chinese watch roof
 for k in range(6):
  z=7.5+k*.18;r=1.7-k*.23
  bpy.ops.mesh.primitive_cone_add(vertices=4,radius1=r*1.414,radius2=(r-.23)*1.414,depth=.20,location=(gx,gy,z),rotation=(0,0,math.pi/4));put(bpy.context.object,'Pagoda roof tier',roofm[k%4])
 beam('Tower roof finial',(gx,gy,8.4),(gx,gy,8.8),.08,brass)
 cube('Indigo gate standard',(gx,gy-1.47,3.9),(.6,.06,2.0),flag)
 cube('Banner gold stripe',(gx,gy-1.52,3.9),(.07,.015,1.6),brass,.005)
# Portal sides and arch
for side in [-1,1]:cube('Portal pier',(side*2.08,gy-.3,1.75),(.85,1.8,3.5),stone[1],.05)
for i in range(13):
 a=i*math.pi/13;b=(i+1)*math.pi/13; vs=[]
 for y in [gy-1.2,gy+.7]:
  for r,t in [(1.68,a),(2.42,a),(2.42,b),(1.68,b)]:vs.append((r*math.cos(t),y,3.1+r*math.sin(t)))
 mesh=bpy.data.meshes.new('Arch voussoir');mesh.from_pydata(vs,[],[(0,1,2,3),(4,7,6,5),(0,4,5,1),(1,5,6,2),(2,6,7,3),(3,7,4,0)]);o=bpy.data.objects.new('Radial gate arch stone',mesh);cols[current].objects.link(o);o.data.materials.append(stone[i%4])
cube('Gate upper lintel',(0,gy,5.6),(5.0,1.5,.5),stone[2])
for x in [-1,0,1]:cube('Gate parapet merlon',(x,gy-.6,6.12),(.6,.65,.65),stone[1])
lantern(-2.1,gy-1.6,3.0);lantern(2.1,gy-1.6,3.0)
# Rocks and foliage on both shelves
current='ENV_Rocks'
for i in range(75):
 x=random.choice([random.uniform(-7,-2.8),random.uniform(3.7,7.0),random.uniform(10.6,11.3)]);y=random.uniform(-8,18)
 if x<-3 and -3<y<3:continue
 z=-.1 if x<7 else -1
 ico('Weathered rock',(x,y,z),(.3+random.random()*.7,.3+random.random()*.7,.2+random.random()*.65),random.choice(stone),1)
current='ENV_Foliage'
for i in range(100):
 x=random.choice([random.uniform(-6.9,-2.9),random.uniform(3.8,7),random.uniform(9.7,11.0)]);y=random.uniform(-8,18)
 if x<-3 and -4<y<4:continue
 z=0 if x<7 else -1
 for j in range(3):ico('Broadleaf shrub',(x+random.uniform(-.3,.3),y+random.uniform(-.3,.3),z+.22+random.uniform(0,.3)),(.32,.38,.3),random.choice(leafm),1)
 for j in range(3):
  h=random.uniform(.2,.5);beam('Grass blade',(x,y,z),(x+random.uniform(-.2,.2),y+random.uniform(-.2,.2),z+h),.022,moss)
# trees with branching trunks and cloudlike canopies
for x,y,s in [(-6,6,1.0),(-6,13,1.3),(6,12,1.0),(10,17,1.2),(-7,-6,.85)]:
 beam('Tree trunk',(x,y,0),(x+.2,y,3*s),.22*s,wood)
 for j in range(7):
  a=j*2.4;dx=math.cos(a)*s;dy=math.sin(a)*s;z=(2.4+j*.2)*s
  beam('Tree bough',(x,y,z-.8),(x+dx,y+dy,z),.10,wood)
  ico('Tree canopy',(x+dx,y+dy,z),(.95*s,.8*s,.6*s),leafm[j%4],2)
# Distant angular mountain silhouettes
current='ENV_Terrain'
for i in range(12):
 x=-22+i*4.3;y=28+random.uniform(0,10);h=random.uniform(7,15)
 ico('Distant karst peak',(x,y,h*.35),(random.uniform(2.5,4),3.5,h*.65),rock,1)
# Rain marks: fine sparse rain streaks in atmosphere, mesh for portable preview
# collision proxies named for module, excluded assembled preview
current='COLLISION'
for n,loc,sc in [('RestStop',(-4.7,0,.14),(5.2,5.5,.35)),('BrokenCart',(-1.8,2.9,.9),(1.9,2.2,1.4)),('WaterBarrel',(-3,-3.5,.88),(1.2,1.2,1.4)),('Path_4m',(-.15,-5.3,-.04),(4.3,4.4,.2)),('Wall_4m',(3.2,-4.5,.6),(1,4.2,1.4)),('Fence_4m',(10.4,-6,-.4),(.2,4.1,1.3)),('LanternPost',(2.55,5,1.5),(.2,.2,3.0))]:
 o=cube('UCX_SM_'+n+'_00',loc,sc,None,0);o.hide_render=True;o.display_type='WIRE';o['purpose']='Simple placeholder collision; validate convex collision and scale in target engine'
# lights and presentation
scene=bpy.context.scene;scene.unit_settings.system='METRIC';scene.unit_settings.scale_length=1
scene.world.color=(.2,.2,.2);scene.world.use_nodes=True;scene.world.node_tree.nodes['Background'].inputs[0].default_value=(.18,.28,.38,1);scene.world.node_tree.nodes['Background'].inputs[1].default_value=.6
ld=bpy.data.lights.new('Cloud break key','AREA');ld.energy=2300;ld.shape='DISK';ld.size=16;o=bpy.data.objects.new('Cloud break key',ld);cols['LIGHTS'].objects.link(o);o.location=(-4,-4,18)
ld=bpy.data.lights.new('Cool horizon','AREA');ld.energy=1700;ld.color=(.45,.69,1);ld.size=12;o=bpy.data.objects.new('Cool horizon',ld);cols['LIGHTS'].objects.link(o);o.location=(4,18,12);o.rotation_euler=(0,0,0)
bpy.ops.object.camera_add(location=(22,-31,24));cam=bpy.context.object;cam.name='CAM_Overview';cam.rotation_euler=(Vector((.5,5,1.9))-cam.location).to_track_quat('-Z','Y').to_euler();cam.data.type='ORTHO';cam.data.ortho_scale=37;scene.camera=cam
scene.render.engine='CYCLES';scene.cycles.device='CPU';scene.cycles.samples=24;scene.cycles.use_denoising=False
scene.render.resolution_x=1600;scene.render.resolution_y=1200;scene.render.resolution_percentage=100
scene.view_settings.view_transform='AgX';scene.render.image_settings.file_format='PNG';scene.render.film_transparent=False
# metadata on interactive assembly roots
for name in ['SM_WaterBarrel','SM_BrokenCart']:
 objs=list(cols[name].objects);root=bpy.data.objects.new(name+'_ROOT',None);cols[name].objects.link(root)
 for o in objs:o.parent=root
 root['Gameplay']='Pushable water barrel' if 'Barrel' in name else 'Broken wheel cart obstacle'
 root['Physics']='Not configured; target engine setup and validation required'
scene['ArtDirection']='Anime-inspired Chinese mountain fantasy; rain-soaked path, roadside shelter and distant south gate'
scene['Status']='SCN_01 modular art prototype. Not production-ready. Engine integration unverified.'
bpy.ops.wm.save_as_mainfile(filepath=ROOT+'/source/SCN_01_Rain_Mountain_Path.blend')
# exports module groups including proxy if provided
report=[]
for name in [n for n in cols if n.startswith('SM_')]:
 bpy.ops.object.select_all(action='DESELECT');objs=list(cols[name].objects)
 for o in objs:o.select_set(True)
 proxy=cols['COLLISION'].objects.get('UCX_'+name+'_00')
 if proxy:proxy.select_set(True)
 bpy.context.view_layer.objects.active=objs[0]
 bpy.ops.export_scene.fbx(filepath=ROOT+'/exports/fbx/'+name+'.fbx',use_selection=True,apply_unit_scale=True,axis_forward='-Z',axis_up='Y',object_types={'MESH','EMPTY'},use_mesh_modifiers=True,add_leaf_bones=False,bake_anim=False)
 report.append({'name':name,'objects':len(objs),'collision_proxy':bool(proxy),'origin':'Scene-space placement preserved; assembly roots present for interactives'})
bpy.ops.object.select_all(action='DESELECT')
for c in cols.values():
 if c.name not in ['COLLISION','LIGHTS']:
  for o in c.objects:
   if o.type in ['MESH','EMPTY']:o.select_set(True)
bpy.ops.export_scene.gltf(filepath=ROOT+'/exports/SCN_01_Assembled.glb',export_format='GLB',use_selection=True,export_draco_mesh_compression_enable=False,export_materials='EXPORT')
with open(ROOT+'/docs/module_manifest.json','w') as f:json.dump({'scene':'SCN_01','units':'meters','modules':report,'limitations':['Prototype geometry; no production topology certification','Procedural Blender material noise may not transfer in FBX/GLB; base colors remain','Collision proxies are simple placeholders, not verified in UE','No gameplay interaction, physics, LODs, lightmaps, navmesh or animation','Gate and backdrop are scenic geometry; collision absent','Individual FBXs preserve scene-space placement']},f,indent=2)
scene.render.filepath=ROOT+'/previews/SCN_01_Overview.png';bpy.ops.render.render(write_still=True)
cam.location=(8,-13,9);cam.rotation_euler=(Vector((-3,.1,2))-cam.location).to_track_quat('-Z','Y').to_euler();cam.data.ortho_scale=15;scene.render.resolution_x=1500;scene.render.resolution_y=1100
scene.render.filepath=ROOT+'/previews/SCN_01_Shelter_Detail.png';bpy.ops.render.render(write_still=True)
print('SCN01 COMPLETE',len(bpy.data.objects))
