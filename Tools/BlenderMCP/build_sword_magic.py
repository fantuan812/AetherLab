"""Original sword-and-magic prototypes, dispatched by the real Blender MCP tool.

Metres, Z up. No external models. New scene; previous files are preserved.
"""
import bpy, bmesh, math, random, json, traceback
from pathlib import Path
from mathutils import Vector

ROOT = Path('C:/ueproject/test')
OUT = ROOT / 'Art/SwordMagic'
for p in (OUT, OUT/'Exports/FBX', OUT/'Exports/GLB', OUT/'Previews'):
    p.mkdir(parents=True, exist_ok=True)
R = random.Random(9122026)
M = {}; ASSETS = []; CHARACTERS = []; LEVEL = []
COL = None; PARTS = []; PREFIX = ''; BONE = None

def progress(stage, **kw):
    (OUT/'build-status.json').write_text(json.dumps(dict(stage=stage, **kw), indent=2), encoding='utf-8')
    print('SWORD_MAGIC', stage, kw, flush=True)

def collection(name, scene):
    c = bpy.data.collections.new(name); scene.collection.children.link(c); return c

def mat(name, color, metal=0, rough=.7, glow=0):
    m = bpy.data.materials.get('M_SM_'+name) or bpy.data.materials.new('M_SM_'+name); m.diffuse_color=(*color,1); m.use_nodes=True
    bs=m.node_tree.nodes.get('Principled BSDF')
    bs.inputs['Base Color'].default_value=(*color,1)
    bs.inputs['Metallic'].default_value=metal; bs.inputs['Roughness'].default_value=rough
    if glow:
        bs.inputs['Emission Color'].default_value=(*color,1); bs.inputs['Emission Strength'].default_value=glow
    M[name]=m; return m

def track(ob, name, material, reactive=None):
    ob.name=PREFIX+name
    for c in list(ob.users_collection): c.objects.unlink(ob)
    COL.objects.link(ob); ob.data.materials.append(M[material]); PARTS.append(ob)
    if BONE:
        vg=ob.vertex_groups.new(name=BONE); vg.add(list(range(len(ob.data.vertices))),1,'REPLACE')
    if reactive: ob['ReactivePreset']=reactive
    return ob

def box(name, loc, size, material='Stone', bevel=.035, rot=0, reactive=None):
    bpy.ops.mesh.primitive_cube_add(size=1, location=loc)
    ob=bpy.context.object; ob.dimensions=size
    bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
    if bevel:
        mod=ob.modifiers.new('Edge highlights','BEVEL'); mod.width=min(bevel,min(size)/5);mod.segments=2
        bpy.ops.object.modifier_apply(modifier=mod.name)
    ob.rotation_euler.z=rot
    return track(ob,name,material,reactive)

def cone(name,loc,r1,r2,h,material='Stone',n=12):
    bpy.ops.mesh.primitive_cone_add(vertices=n,radius1=r1,radius2=r2,depth=h,location=loc)
    return track(bpy.context.object,name,material)

def ico(name,loc,scale,material='Stone',sub=1):
    bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=sub,radius=1,location=loc)
    ob=bpy.context.object;ob.scale=scale
    bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
    return track(ob,name,material)

def rod(name,a,b,r,material='Wood',n=10,r2=None):
    a,b=Vector(a),Vector(b);ob=cone(name,(a+b)/2,r,r if r2 is None else r2,(b-a).length,material,n)
    ob.rotation_euler=(b-a).to_track_quat('Z','Y').to_euler();return ob

def mesh(name,verts,faces,material):
    d=bpy.data.meshes.new(name);d.from_pydata(verts,[],faces);d.update()
    bm=bmesh.new();bm.from_mesh(d);bmesh.ops.recalc_face_normals(bm,faces=bm.faces);bm.to_mesh(d);bm.free()
    ob=bpy.data.objects.new(name,d);COL.objects.link(ob);return track(ob,name,material)

def prism(name, outline, depth, material, y=0):
    # Outline in X/Z, extruded along Y. Closed shell.
    v=[(x,y+d,z) for d in (-depth/2,depth/2) for x,z in outline];n=len(outline)
    f=[tuple(reversed(range(n))),tuple(range(n,n*2))]
    f += [(i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n)]
    return mesh(name,v,f,material)

def ring(name,loc,rad,tube,material='Bronze',rot=(0,0,0)):
    bpy.ops.mesh.primitive_torus_add(major_segments=32,minor_segments=6,major_radius=rad,minor_radius=tube,location=loc,rotation=rot)
    return track(bpy.context.object,name,material)

def lathe(name,profile,material,loc=(0,0,0),n=32):
    v=[(loc[0]+r*math.cos(i*math.tau/n),loc[1]+r*math.sin(i*math.tau/n),loc[2]+z) for r,z in profile for i in range(n)]
    f=[(j*n+i,j*n+(i+1)%n,(j+1)*n+(i+1)%n,(j+1)*n+i) for j in range(len(profile)-1) for i in range(n)]
    f += [tuple(reversed(range(n))),tuple((len(profile)-1)*n+i for i in range(n))]
    return mesh(name,v,f,material)

def arch(name, center, width=4, rise=2, leg=3, depth=.7, yaw=0):
    # Voussoirs, with a real clear passage (no opaque wall behind it).
    start=len(PARTS);x,y,z=center;radius=width/2;th=.42
    for sign in (-1,1):
        box(name+'_Pier',(x+sign*(radius+th/2),y,z+leg/2),(th,depth,leg),'StoneLight')
        box(name+'_Foot',(x+sign*(radius+th/2),y,z+.15),(.8,depth+.25,.3),'StoneTrim')
    for i in range(11):
        a=i*math.pi/11+.01;b=(i+1)*math.pi/11-.01
        outline=[(x+rr*math.cos(t),z+leg+rise*(rr/radius)*math.sin(t)) for rr,t in ((radius,a),(radius,b),(radius+th,b),(radius+th,a))]
        prism(name+'_ArchStone',outline,depth,'StoneLight' if i%3 else 'StoneTrim',y)
    if yaw:
        from mathutils import Matrix
        bpy.context.view_layer.update()
        T=Matrix.Translation(Vector(center));A=T@Matrix.Rotation(yaw,4,'Z')@T.inverted()
        for ob in PARTS[start:]:
            ob.matrix_world=A@ob.matrix_world

def select(obs):
    bpy.ops.object.select_all(action='DESELECT')
    for ob in obs:ob.hide_set(False);ob.select_set(True)
    bpy.context.view_layer.objects.active=obs[0]

def join_parts(name,parts,pivot=(0,0,0),uv=True):
    select(parts);bpy.ops.object.join();ob=bpy.context.object;ob.name=name
    bpy.context.scene.cursor.location=pivot;bpy.ops.object.origin_set(type='ORIGIN_CURSOR')
    bpy.ops.object.transform_apply(location=False,rotation=True,scale=True)
    if uv:
        bpy.ops.object.mode_set(mode='EDIT');bpy.ops.mesh.select_all(action='SELECT')
        bpy.ops.uv.smart_project(angle_limit=math.radians(66),island_margin=.012)
        bpy.ops.object.mode_set(mode='OBJECT');ob.data.uv_layers[0].name='UV0'
        ob.data.uv_layers.new(name='UV1_Lightmap',do_init=True)
    return ob

def export(name,obs):
    select(obs)
    bpy.ops.export_scene.fbx(filepath=str(OUT/'Exports/FBX'/f'{name}.fbx'),use_selection=True,object_types={'MESH','ARMATURE'},apply_unit_scale=True,apply_scale_options='FBX_SCALE_UNITS',axis_forward='-Y',axis_up='Z',add_leaf_bones=False,bake_anim=False,use_custom_props=True)
    bpy.ops.export_scene.gltf(filepath=str(OUT/'Exports/GLB'/f'{name}.glb'),use_selection=True,export_format='GLB',export_animations=False)

def stat(ob):
    bpy.context.view_layer.update()
    ob.data.calc_loop_triangles()
    return dict(name=ob.name,triangles=len(ob.data.loop_triangles),dimensions_m=list(ob.dimensions),uv_layers=len(ob.data.uv_layers))

def banner(x,y,z,material='Teal',height=2):
    rod('BannerPole',(x,y,z),(x,y,z+height+1),.05,'Bronze')
    rod('BannerArm',(x-.65,y,z+height+.6),(x+.65,y,z+height+.6),.045,'Bronze')
    prism('SwallowtailBanner',[(x-.57,z+height+.5),(x+.57,z+height+.5),(x+.57,z+.4),(x,z+.7),(x-.57,z+.4)],.035,material,y)
    box('BannerSigil',(x,y-.03,z+height-.15),(.075,.035,.6),'Gold',.01)

def lantern(x,y,z):
    cone('LanternFoot',(x,y,z+.13),.5,.4,.26,'StoneTrim')
    cone('LanternStem',(x,y,z+1),.17,.13,1.5,'Bronze')
    cone('LanternBowl',(x,y,z+1.9),.3,.46,.25,'Bronze')
    ico('WarmFlame',(x,y,z+2.25),(.22,.22,.52),'Amber',2)

def sword(x=0,y=0,z=0):
    prism('SwordBlade',[(x-.065,z+.22),(x+.065,z+.22),(x+.05,z+1.02),(x,z+1.22),(x-.05,z+1.02)],.04,'Silver',y)
    box('SwordFuller',(x,y-.024,z+.65),(.015,.012,.72),'Steel',.002)
    box('SwordGuard',(x,y,z+.22),(.36,.09,.06),'Gold',.025)
    rod('SwordGrip',(x,y,z),(x,y,z+.2),.043,'Leather')
    ico('SwordPommel',(x,y,z-.04),(.075,.06,.07),'Gold')

def shield(x=0,y=0,z=0):
    outline=[(-.34,.78),(.34,.78),(.39,.48),(.24,.04),(0,-.2),(-.24,.04),(-.39,.48)]
    prism('ShieldRim',[(x+a,z+b) for a,b in outline],.12,'Gold',y)
    prism('ShieldFace',[(x+a*.88,z+.3+(b-.3)*.88) for a,b in outline],.045,'Teal',y-.084)
    box('ShieldOath',(x,y-.12,z+.38),(.045,.03,.55),'Ivory',.01)
    box('ShieldCross',(x,y-.125,z+.52),(.3,.03,.045),'Ivory',.01)
    ico('ShieldBoss',(x,y-.16,z+.31),(.105,.065,.105),'Gold',2)

def character(name,boss=False):
    global COL,PARTS,PREFIX,BONE
    PARTS=[];PREFIX=name+'_';COL=collection(name,charscene)
    BONE='pelvis'
    # Polygonal fitted silhouette, separate articulated armour pieces.
    cone('BeltHip',(0,0,.88),.24,.22,.19,'Leather')
    box('Belt',(0,-.005,1),(.49,.29,.08),'Bronze',.025)
    ico('Buckle',(0,-.18,1),(.085,.04,.085),'Gold',2)
    cone('MailSkirt',(0,0,.80),.30,.23,.3,'Bronze' if boss else 'Teal',12)
    for side,sign in [('l',1),('r',-1)]:
        hip=(sign*.14,0,.83);knee=(sign*.17,-.015,.48);ankle=(sign*.18,0,.14)
        BONE='thigh_'+side;rod('Thigh',hip,knee,.115,'Leather',12,r2=.1)
        ico('ThighPlate',(sign*.18,-.045,.66),(.135,.105,.2),'Bronze' if boss else 'Steel',2)
        BONE='calf_'+side;rod('Shin',knee,ankle,.095,'Steel',12,r2=.08)
        ico('Knee',(sign*.17,-.09,.48),(.12,.095,.115),'Gold' if boss else 'Silver',2)
        box('Greave',(sign*.18,-.07,.30),(.16,.09,.24),'Bronze' if boss else 'Silver',.035)
        BONE='foot_'+side;box('Boot',(sign*.18,-.08,.075),(.20,.37,.15),'Steel',.035)
    BONE='spine'
    cone('UnderTorso',(0,0,1.16),.20,.28,.36,'Leather',12).scale.y=.55
    ico('Breastplate',(0,-.035,1.25),(.29,.18,.31),'Bronze' if boss else 'Silver',2)
    prism('ChestTabard',[(-.105,1.45),(.105,1.45),(.11,1.03),(0,.96),(-.11,1.03)],.025,'Crimson' if boss else 'Teal',-.251)
    box('ChestRune',(0,-.275,1.26),(.025,.018,.22),'Amber' if boss else 'Gold',.002)
    # Folded cape has closed thickness and remains a rigid prototype panel.
    prism('Cape',[(-.25,1.43),(.25,1.43),(.36,.58),(.14,.64),(0,.53),(-.34,.62)],.05,'Crimson' if boss else 'Teal',.19)
    BONE='neck';rod('Neck',(0,0,1.48),(0,0,1.58),.1,'Leather')
    BONE='head';ico('Helm',(0,0,1.70),(.18,.16,.22),'Bronze' if boss else 'Steel',2)
    box('Visor',(0,-.145,1.69),(.31,.06,.055),'Void',.012)
    box('Brow',(0,-.166,1.74),(.32,.035,.04),'Gold' if boss else 'Silver',.012)
    box('Nasal',(0,-.176,1.66),(.035,.04,.15),'Bronze' if boss else 'Silver',.008)
    if boss:
        ring('BellCrown',(0,0,1.86),.17,.035,'Gold')
        for i in range(5):
            a=i*math.tau/5;cone('CrownPoint',(.16*math.cos(a),.16*math.sin(a),1.95),.045,0,.19,'Gold',5)
    else:
        prism('HelmCrest',[(-.04,1.84),(.04,1.84),(.035,2.00),(-.035,1.96)],.18,'Teal',.015)
    for side,sign in [('l',1),('r',-1)]:
        shoulder=(sign*.30,0,1.43);elbow=(sign*.43,-.015,1.16);wrist=(sign*.52,-.08,.94)
        BONE='upperarm_'+side;rod('UpperArm',shoulder,elbow,.085,'Leather',12)
        ico('Pauldron',(sign*.32,0,1.44),(.20 if boss else .16,.19,.14),'Bronze' if boss else 'Steel',2)
        if boss:
            for dy in (-.08,.08):cone('ShoulderSpike',(sign*.38,dy,1.62),.06,0,.2,'Gold',5)
        BONE='lowerarm_'+side;rod('Vambrace',elbow,wrist,.10,'Bronze' if boss else 'Silver',12,r2=.07)
        BONE='hand_'+side;ico('Gauntlet',(sign*.53,-.09,.9),(.085,.08,.095),'Steel',2)
    if boss:
        BONE='hand_r';rod('HammerShaft',(-.55,-.1,.45),(-.55,-.1,1.80),.045,'Wood')
        box('BellHammer',(-.55,-.1,1.72),(.65,.32,.34),'Bronze',.07)
        for xx in (-.92,-.18):box('HammerCap',(xx,-.1,1.72),(.10,.39,.42),'Gold',.035)
        ring('HammerSeal',(-.55,-.28,1.72),.11,.025,'Amber',(math.pi/2,0,0))
    else:
        BONE='hand_r';sword(-.55,-.1,.80)
        BONE='hand_l';shield(.59,-.20,.65)
    BONE=None
    body=join_parts('SK_'+name,PARTS)
    factor=1.62 if boss else .925
    for v in body.data.vertices:v.co*=factor
    specs=[('root',(0,0,0),(0,0,.15),None),('pelvis',(0,0,.84),(0,0,1.0),'root'),('spine',(0,0,1),(0,0,1.45),'pelvis'),('neck',(0,0,1.45),(0,0,1.57),'spine'),('head',(0,0,1.57),(0,0,1.89),'neck')]
    for side,s in [('l',1),('r',-1)]:
        specs += [('thigh_'+side,(s*.14,0,.84),(s*.17,-.015,.48),'pelvis'),('calf_'+side,(s*.17,-.015,.48),(s*.18,0,.14),'thigh_'+side),('foot_'+side,(s*.18,0,.14),(s*.18,-.25,.07),'calf_'+side),('upperarm_'+side,(s*.30,0,1.43),(s*.43,-.015,1.16),'spine'),('lowerarm_'+side,(s*.43,-.015,1.16),(s*.52,-.08,.94),'upperarm_'+side),('hand_'+side,(s*.52,-.08,.94),(s*.54,-.1,.82),'lowerarm_'+side)]
    arm=bpy.data.armatures.new('Rig_'+name);rig=bpy.data.objects.new('Rig_'+name,arm);COL.objects.link(rig)
    select([rig]);bpy.ops.object.mode_set(mode='EDIT')
    for bn,a,b,parent in specs:
        bone=arm.edit_bones.new(bn);bone.head=Vector(a)*factor;bone.tail=Vector(b)*factor
        if parent:bone.parent=arm.edit_bones[parent]
    bpy.ops.object.mode_set(mode='OBJECT');rig.show_in_front=True
    mod=body.modifiers.new('Prototype rigid armour rig','ARMATURE');mod.object=rig;body.parent=rig
    body['PrototypeRig']='Rigid part weights; no production deformation, fingers or animations.'
    export('SK_'+name,[body,rig])
    record=stat(body);record.update(bones=len(arm.bones),height_m=max(v.co.z for v in body.data.vertices),rig='rigid_part_weights',animations=0)
    CHARACTERS.append(record)
    rig.location=(-1.55 if not boss else 1.55,0,0)
    return rig,body

def setup_scene(scene):
    scene.unit_settings.system='METRIC';scene.unit_settings.scale_length=1
    scene.render.engine='CYCLES';scene.cycles.samples=32;scene.cycles.use_denoising=True
    try:
        pref=bpy.context.preferences.addons['cycles'].preferences;pref.compute_device_type='OPTIX';pref.get_devices()
        for dev in pref.devices:dev.use=dev.type!='CPU'
        if any(d.use for d in pref.devices):scene.cycles.device='GPU'
    except Exception:pass
    scene.world=bpy.data.worlds.new(scene.name+'_World');scene.world.use_nodes=True
    scene.world.node_tree.nodes['Background'].inputs[0].default_value=(.22,.31,.42,1)
    scene.world.node_tree.nodes['Background'].inputs[1].default_value=.5
    scene.render.resolution_x=1800;scene.render.resolution_y=1400;scene.render.resolution_percentage=100

def camera(scene,name,loc,target,ortho):
    d=bpy.data.cameras.new(name);ob=bpy.data.objects.new(name,d);scene.collection.objects.link(ob);ob.location=loc
    ob.rotation_euler=(Vector(target)-ob.location).to_track_quat('-Z','Y').to_euler();d.type='ORTHO';d.ortho_scale=ortho;d.clip_end=500;scene.camera=ob;return ob

def light(scene,loc,target,power,size,color):
    d=bpy.data.lights.new('Softbox','AREA');d.energy=power;d.shape='DISK';d.size=size;d.color=color
    ob=bpy.data.objects.new('Softbox',d);scene.collection.objects.link(ob);ob.location=loc;ob.rotation_euler=(Vector(target)-ob.location).to_track_quat('-Z','Y').to_euler()

def label(scene,text,loc,size=.3):
    d=bpy.data.curves.new(text,'FONT');d.body=text;d.size=size;d.align_x='CENTER';d.extrude=.001
    ob=bpy.data.objects.new('Label_'+text,d);scene.collection.objects.link(ob);ob.location=loc;d.materials.append(M['Ivory']);return ob

def build():
    global COL,PARTS,PREFIX,charscene,levelscene,BONE
    progress('starting')
    # Only replace the two scenes owned by this builder on a deliberate rebuild.
    previous=[bpy.data.scenes[n] for n in ('SM_01_BrokenBellAbbey','SM_02_Characters') if n in bpy.data.scenes]
    owned=set(ob for sc in previous for ob in sc.objects)
    for sc in previous:bpy.data.scenes.remove(sc)
    for ob in owned:
        if not ob.users_scene:bpy.data.objects.remove(ob,do_unlink=True)
    levelscene=bpy.data.scenes.new('SM_01_BrokenBellAbbey');bpy.context.window.scene=levelscene;setup_scene(levelscene)
    for args in [('Stone',(.24,.29,.30),0,.85),('StoneLight',(.39,.43,.41),0,.85),('StoneTrim',(.56,.55,.46),0,.8),('Ground',(.12,.20,.18),0,.95),('Rock',(.075,.12,.14),0,.93),('Wood',(.24,.105,.045),0,.8),('Bronze',(.34,.19,.075),.75,.4),('Gold',(.68,.40,.10),.75,.32),('Silver',(.58,.66,.69),.75,.3),('Steel',(.13,.19,.23),.8,.4),('Leather',(.052,.043,.033),0,.88),('Teal',(.015,.20,.23),0,.8),('Crimson',(.32,.028,.024),0,.85),('Ivory',(.76,.72,.52),0,.65),('Void',(.009,.012,.014),0,.6),('Water',(.018,.27,.33),.25,.2),('Ice',(.24,.66,.74),.15,.22),('Amber',(1,.29,.035),.15,.3,2.5)]:mat(*args)
    COL=collection('01_Terrain_and_Water',levelscene);PARTS=[];PREFIX='LV_'
    box('DioramaRock',(0,2,-2.5),(48,76,3),'Rock',1)
    box('SouthBank',(0,-26,-.6),(46,15,1.2),'Ground',.5)
    box('AbbeyBank',(0,13,-.6),(46,58,1.2),'Ground',.5)
    box('CanalBed',(0,-16.5,-.9),(46,4.6,.4),'Stone',.15)
    water=box('CanalWater',(0,-16.5,-.55),(46,4.5,.08),'Water',.01,reactive='Water')
    water['Gameplay']='Freezeable shallow crossing; visual mesh only'
    for i in range(26):
        x=R.uniform(-23,23);y=R.choice((-34,39));ico('Cliff',(x,y,-1.4),(R.uniform(1.2,3),R.uniform(1,2),R.uniform(1,2)),'Rock')
    # Readable paving leads from the southern start through the courtyard.
    for y in list(range(-31,-19,2))+list(range(-12,15,2)):
        for x in (-1.5,1.5):box('PathStone',(x,y,.025),(2.8,1.85,.12),'StoneLight',.045)
    COL=collection('02_Broken_Bridge_and_Dry_Detour',levelscene);PARTS=[]
    for y in (-19.2,-13.8):box('BridgeAbutment',(0,y,-.3),(5,1.2,1.2),'StoneTrim',.08)
    for x in (-1.7,1.7):
        rod('BridgeBeam',(x,-19.4,.05),(x,-13.6,.05),.14,'Wood')
        for y in (-19,-14):
            rod('RopePost',(x,y,.1),(x,y,1.3),.11,'Wood')
        rod('CuttableRope',(x,-19,1.25),(x,-14,.9),.035,'Wood')
    for i in range(11):
        if i in (4,5,6):continue
        ob=box('BridgePlank',(0,-19+i*.49,.17),(3.5,.45,.19),'Wood',.02,rot=R.uniform(-.025,.025),reactive='Wood');ob['Gameplay']='Individually replaceable bridge plank'
    box('FrostCrossing',(6,-16.5,-.40),(2.8,4.7,.22),'Ice',.08)
    for y in (-20,-18,-16,-14,-12):box('DryDetour',(-19,y,-.05),(4,2.1,.4),'StoneLight',.08)
    COL=collection('03_Outer_Courtyard',levelscene);PARTS=[]
    box('OuterCourt',(0,-5,-.05),(34,17,.2),'Stone',.1)
    for y in range(-12,3,3):
        for x in range(-15,16,3):
            if R.random()<.3:box('WornPaver',(x,y,.075),(2.8,2.8,.06),'StoneLight',.02)
    arch('EntryGate',(0,-12,0),5,2.6,3.4,.95)
    for x in (-10.5,10.5):
        box('GateWall',(x,-12,1.7),(13,.8,3.4),'Stone',.05)
        for bx in (-4,0,4):box('Crenel',(x+bx,-12,3.65),(1.3,.95,.7),'StoneTrim',.035)
    for x in (-17,17):
        box('CourtWall',(x,-5,1.05),(.85,14,2.1),'Stone',.06)
        for y in (-10,-4,2):box('Buttress',(x,y,1.35),(1.8,1,2.7),'StoneLight',.05)
    for x,y in [(-7,-4),(10,-1),(-11,1)]:
        box('CoverCrate',(x,y,.6),(1.3,1.1,1.2),'Wood',.04,reactive='Wood')
        for z in (.15,1.05):box('CrateBand',(x,y-.565,z),(1.38,.08,.12),'Bronze',.01)
    box('ConductivePuddle',(5,-5,.10),(6,4,.06),'Water',.15,reactive='Water')
    for x in (3.8,5.2):rod('DrainConductor',(x,-7.5,.15),(x,-1.5,.15),.055,'Bronze')
    for x in (-4,4):banner(x,-10,.1);lantern(x,0,.1)
    COL=collection('04_Cloister_and_Rescue',levelscene);PARTS=[]
    for x in (-15,15):
        box('ArcadeFloor',(x,9,.12),(7,16,.25),'StoneTrim',.06)
        for y in (3,8,13):arch('CloisterBay',(x,y,.25),4.2,1.8,2.6,.55,math.pi/2)
        box('CloisterRearWall',(x+(-3 if x<0 else 3),9,2.1),(.7,16,4),'Stone',.05)
        box('ArcadeRoof',(x,9,5.15),(7.6,16.5,.30),'Teal',.1)
        for y in (2,7,12,17):box('RoofCorbel',(x,y,4.55),(7.3,.25,.35),'Bronze',.04)
    # Central cistern is hollow and conveys a finite water supply.
    for x,y,sx,sy in [(-7,7,5,.35),(-7,11,5,.35),(-9.3,9,.35,4),(-4.7,9,.35,4)]:box('CisternRim',(x,y,.7),(sx,sy,1.4),'StoneLight',.08)
    box('CisternWater',(-7,9,1.05),(4.2,3.6,.06),'Water',.01,reactive='Water')
    box('ApprenticeBench',(15,11,.5),(1.8,.7,1),'Wood',.06)
    ico('ApprenticeHead',(15,11,1.65),(.18,.18,.23),'Ivory',2)
    cone('ApprenticeCloak',(15,11,1.05),.35,.18,.8,'Teal')
    box('OathRecordPedestal',(15,5,.65),(.9,.9,1.3),'StoneTrim',.1)
    book=box('OathRecord',(15,5,1.36),(.65,.5,.12),'Ivory',.015);book['Gameplay']='Old oath evidence location'
    COL=collection('05_Bell_Knight_Arena',levelscene);PARTS=[]
    for i in range(5):box('ArenaStep',(0,16+i*.65,.12*(i+1)),(7,1.0,.24*(i+1)),'StoneLight',.04)
    cone('ArenaBase',(0,27,.55),11.5,11.5,1.1,'Stone',64)
    ring('ArenaOathRing',(0,27,1.12),8.3,.055,'Gold')
    for i in range(12):
        a=i*math.tau/12;box('ArenaRune',(8.3*math.cos(a),27+8.3*math.sin(a),1.14),(.18,.65,.05),'Gold',.01,rot=a)
    for x in (-11,11):
        box('ArenaSideWall',(x,28,2.15),(.8,16,2.2),'Stone',.08)
        for y in (21,28,35):box('ArenaPier',(x,y,3),(1.6,1.4,4),'StoneLight',.06)
    # Belfry: open arched crown, visible bronze bell, four sloping roof faces.
    for x in (-3.2,3.2):
        for y in (31,37):
            box('TowerPier',(x,y,7),(1.1,1.1,12),'StoneLight',.08)
            for z in (2,6,10,12.8):box('TowerCornice',(x,y,z),(1.45,1.45,.35),'StoneTrim',.04)
    for y in (31,37):arch('BelfryArch',(0,y,9.2),5.4,2.3,2.2,.75)
    for x in (-3.2,3.2):rod('BellYoke',(x,31,12.4),(x,37,12.4),.22,'Wood')
    rod('BellAxle',(-3.2,34,12.4),(3.2,34,12.4),.24,'Wood')
    rod('BellSuspension',(0,34,11.25),(0,34,12.4),.10,'Steel')
    lathe('GreatBell',[(.95,0),(1.5,.15),(1.42,.32),(1.05,.8),(.85,1.9),(.65,2.35),(.25,2.55),(.17,2.58)],'Bronze',(0,34,8.8))
    for z in (9.15,9.45,10.85):ring('BellInscription',(0,34,z),1.33 if z<10 else .89,.04,'Gold')
    rod('BellClapper',(0,34,8.6),(0,34,10.3),.10,'Steel');ico('ClapperBall',(0,34,8.6),(.25,.25,.3),'Steel',2)
    cone('BelfryRoof',(0,34,15),5.1,0,4.3,'Teal',4).rotation_euler.z=math.pi/4
    rod('RoofFinial',(0,34,17),(0,34,18.3),.07,'Gold');ico('StarFinial',(0,34,18.4),(.32,.32,.45),'Gold')
    for x in (-7,7):banner(x,33,1.1,'Crimson',3);lantern(x,22,1.1)
    box('SigilAltar',(0,36,1.7),(2.5,1.4,1.2),'StoneTrim',.1)
    ico('AncientSigil',(0,36,2.65),(.4,.2,.6),'Amber',2)
    # A small witness bell and an obvious return gate on the eastern dry path.
    arch('ReturnGate',(19,19,0),3,1.8,2.4,.8)
    lathe('WitnessBell',[(.25,0),(.37,.06),(.25,.30),(.14,.6),(.06,.68)],'Gold',(8,17,1.6),20)
    rod('WitnessStand',(7.4,17,0),(7.4,17,2.5),.08,'Wood');rod('WitnessArm',(7.4,17,2.5),(8.2,17,2.5),.08,'Wood')
    for y in range(-10,20,3):box('ReturnDryPath',(20,y,.05),(3,2.8,.2),'StoneLight',.04)
    # Place a few fallen masonry clusters outside the walking line.
    COL=collection('06_Ruins_and_Set_Dressing',levelscene);PARTS=[]
    for i in range(35):
        x=R.choice((-1,1))*R.uniform(10,15);y=R.uniform(-8,16)
        ob=box('Rubble',(x,y,R.uniform(.12,.3)),(R.uniform(.3,.9),R.uniform(.3,.8),R.uniform(.25,.6)),'StoneTrim',.035,rot=R.uniform(0,math.tau))
    # Give all editable meshes UV channels, category and stable gameplay labels.
    progress('unwrap_level')
    meshes=[o for o in levelscene.objects if o.type=='MESH']
    for ob in meshes:
        if not ob.data.uv_layers:ob.data.uv_layers.new(name='UV0')
        # Simple box-projected UVs per face for the blockout's flat PBR materials.
        for poly in ob.data.polygons:
            ax=max(range(3),key=lambda a:abs(poly.normal[a]));axes=[a for a in range(3) if a!=ax]
            for li in poly.loop_indices:
                v=ob.data.vertices[ob.data.loops[li].vertex_index].co;ob.data.uv_layers[0].data[li].uv=(v[axes[0]],v[axes[1]])
        ob['Prototype']='Broken Bell Abbey';ob['Units']='metres'
    LEVEL.extend(stat(o) for o in meshes)
    export('SM_BrokenBellAbbey_Layout',meshes)
    camera(levelscene,'CAM_Abbey_Overview',(62,-79,71),(0,4,3),93)
    light(levelscene,(-25,-22,52),(0,4,0),85000,35,(1,.78,.52));light(levelscene,(26,20,36),(0,8,0),65000,28,(.49,.73,1))
    levelscene.render.filepath=str(OUT/'Previews/01_Abbey_Overview.png')
    progress('build_characters',level_meshes=len(meshes))
    charscene=bpy.data.scenes.new('SM_02_Characters');bpy.context.window.scene=charscene;setup_scene(charscene)
    hero=character('Oathwanderer',False);boss=character('BellKnight_Auren',True)
    COL=collection('Character_Presentation',charscene);PREFIX='STUDIO_';PARTS=[]
    for x,rad in [(-1.55,1.2),(1.55,1.5)]:
        cone('Plinth',(x,0,-.13),rad,rad,.24,'Rock',64)
        ring('PlinthTrim',(x,0,-.035),rad-.06,.022,'Gold')
    box('StudioFloor',(0,0,-.31),(200,200,.3),'Void',0)
    label(charscene,'OATHWANDERER',(-1.55,-1.05,.005),.16)
    label(charscene,'AUREN / BELL KNIGHT',(1.55,-1.34,.005),.17)
    camera(charscene,'CAM_Character_Lineup',(6,-15,6),(0,0,1.35),7.8)
    light(charscene,(-4,-6,7),(0,0,1.2),1600,5,(1,.79,.57));light(charscene,(5,-2,5),(0,0,1.4),1100,4,(.55,.8,1));light(charscene,(1,4,6),(0,0,1.6),1800,3,(.9,.5,.24))
    charscene.render.resolution_x=1800;charscene.render.resolution_y=1300
    charscene.render.filepath=str(OUT/'Previews/02_Character_Lineup.png')
    # Character instances at real scale in the adventure scene.
    bpy.context.window.scene=levelscene
    for rig,body,loc in [(hero[0],hero[1],(0,-25,.12)),(boss[0],boss[1],(0,27,1.12))]:
        inst=body.copy();inst.data=body.data;levelscene.collection.objects.link(inst);inst.parent=None
        for mod in list(inst.modifiers):inst.modifiers.remove(mod)
        inst.location=loc;inst.name='Preview_'+body.name;inst['Gameplay']='Static placement reference; rigged source is in character scene'
    levelscene['GameplayStatus']='Art blockout only. Hook up existing UE gameplay components separately.'
    levelscene['SuggestedRoute']='Entry > bridge/ice/detour > courtyard > cloister rescue > bell arena > east return'
    levelscene['Scale']='48 x 76 m compact spatial prototype'
    manifest=dict(version=1,source='Blender MCP execute_blender_code',units='metres',level=dict(meshes=len(LEVEL),triangles=sum(r['triangles'] for r in LEVEL),footprint_m=[48,76],objects=LEVEL),characters=CHARACTERS,notes=['No downloaded assets','Level FBX is a multi-mesh layout, not modular asset-at-origin exports','Character weights are rigid armour parts; no animation or production skinning','Water, ice and destructible pieces are visual proxies; UE gameplay not linked','Level UV0 is tiled box projection; UE must generate lightmap UVs if using baked lighting'])
    (OUT/'asset-manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
    # Set a useful modelling viewport and preserve both scenes in one source file.
    for screen in bpy.data.screens:
        for area in screen.areas:
            if area.type=='VIEW_3D':
                space=area.spaces.active;space.clip_end=500;space.shading.type='MATERIAL'
                space.region_3d.view_distance=90;space.region_3d.view_location=(0,5,3)
                space.region_3d.view_rotation=levelscene.camera.rotation_euler.to_quaternion()
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'BrokenBellAbbey_Characters.blend'))
    progress('built',level_meshes=len(LEVEL),characters=CHARACTERS)

try:build()
except Exception:
    progress('failed',error=traceback.format_exc());raise
