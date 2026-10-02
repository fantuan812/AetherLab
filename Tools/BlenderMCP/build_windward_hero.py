"""Hand-authored original stylized adventurer. Run with Blender 5.2 --background.

No downloaded meshes, generators, textures, or copyrighted character assets.
Source geometry, explicit skin weights, editable FK/leg-IK rig and demo actions.
"""
import bpy
import bmesh
import math
import json
import sys
from pathlib import Path
from mathutils import Vector, Quaternion

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "Art" / "WindwardHero"
OUT.mkdir(parents=True, exist_ok=True)
(OUT / "Exports").mkdir(exist_ok=True)
(OUT / "Previews").mkdir(exist_ok=True)

MATS = {}
PARTS = []
RIG = None
CHAR = None


def material(name, color, metallic=0, roughness=.65):
    m = bpy.data.materials.new(name)
    m.diffuse_color = (*color, 1)
    m.use_nodes = True
    p = m.node_tree.nodes.get("Principled BSDF")
    p.inputs["Base Color"].default_value = (*color, 1)
    p.inputs["Metallic"].default_value = metallic
    p.inputs["Roughness"].default_value = roughness
    MATS[name] = m
    return m


def mesh(name, verts, faces, mat, weights=None, smooth=True):
    data = bpy.data.meshes.new(name)
    data.from_pydata(verts, [], faces)
    data.update()
    bm = bmesh.new()
    bm.from_mesh(data)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.to_mesh(data)
    bm.free()
    ob = bpy.data.objects.new(name, data)
    CHAR.objects.link(ob)
    ob.data.materials.append(MATS[mat])
    for poly in data.polygons:
        poly.use_smooth = smooth
    if isinstance(weights, str):
        weights = [{weights: 1} for _ in verts]
    if weights:
        for i, influences in enumerate(weights):
            for bone, value in influences.items():
                vg = ob.vertex_groups.get(bone) or ob.vertex_groups.new(name=bone)
                vg.add([i], value, 'REPLACE')
    PARTS.append(ob)
    return ob


def ellipsoid(name, loc, scale, mat, bone, rings=12, segments=24):
    verts = [(loc[0], loc[1], loc[2] + scale[2])]
    for j in range(1, rings):
        a = math.pi * j / rings
        for i in range(segments):
            t = math.tau * i / segments
            verts.append((loc[0] + scale[0]*math.sin(a)*math.cos(t),
                          loc[1] + scale[1]*math.sin(a)*math.sin(t),
                          loc[2] + scale[2]*math.cos(a)))
    verts.append((loc[0], loc[1], loc[2] - scale[2]))
    faces = [(0, 1+i, 1+(i+1)%segments) for i in range(segments)]
    for j in range(rings-2):
        k=1+j*segments
        faces += [(k+i,k+(i+1)%segments,k+segments+(i+1)%segments,k+segments+i) for i in range(segments)]
    k=1+(rings-2)*segments
    faces += [(len(verts)-1,k+(i+1)%segments,k+i) for i in range(segments)]
    return mesh(name,verts,faces,mat,bone)


def loft(name, centers, radii, mat, weights, segments=16, axis=None, caps=True, smooth=True):
    # Explicit quad rings with a stable cross section, suitable for bending.
    centers = [Vector(v) for v in centers]
    tangent = Vector(axis) if axis else (centers[-1]-centers[0]).normalized()
    ref = Vector((0,1,0))
    if abs(tangent.dot(ref))>.95:
        ref=Vector((1,0,0))
    u = tangent.cross(ref).normalized()
    v = tangent.cross(u).normalized()
    verts=[]; w=[]
    for j, (c, rad) in enumerate(zip(centers,radii)):
        rx,ry = rad
        for i in range(segments):
            a=math.tau*i/segments
            verts.append(tuple(c + u*rx*math.cos(a) + v*ry*math.sin(a)))
            w.append(weights[j] if isinstance(weights,list) else {weights:1})
    faces=[]
    for j in range(len(centers)-1):
        faces += [(j*segments+i,j*segments+(i+1)%segments,(j+1)*segments+(i+1)%segments,(j+1)*segments+i) for i in range(segments)]
    if caps:
        faces += [tuple(reversed(range(segments))),tuple((len(centers)-1)*segments+i for i in range(segments))]
    return mesh(name,verts,faces,mat,w,smooth)


def box(name,loc,scale,mat,bone,bevel=.008):
    bpy.ops.mesh.primitive_cube_add(size=1,location=loc)
    ob=bpy.context.object
    ob.name=name; ob.dimensions=scale
    bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
    if bevel:
        mod=ob.modifiers.new('Soft crafted edges','BEVEL'); mod.width=bevel; mod.segments=2
        bpy.ops.object.modifier_apply(modifier=mod.name)
    for c in list(ob.users_collection): c.objects.unlink(ob)
    CHAR.objects.link(ob); ob.data.materials.append(MATS[mat]); PARTS.append(ob)
    if bone:
        g=ob.vertex_groups.new(name=bone); g.add(list(range(len(ob.data.vertices))),1,'REPLACE')
    return ob


def prism(name, outline, y, depth, mat, bone, bevel=0):
    n=len(outline)
    verts=[(x,y+d,z) for d in [-depth/2,depth/2] for x,z in outline]
    faces=[tuple(reversed(range(n))),tuple(range(n,n*2))]
    faces += [(i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n)]
    ob=mesh(name,verts,faces,mat,bone,False)
    if bevel:
        bpy.context.view_layer.objects.active=ob
        m=ob.modifiers.new('Edge glint','BEVEL'); m.width=bevel;m.segments=2
        bpy.ops.object.modifier_apply(modifier=m.name)
    return ob


def ribbon(name, pts, width, mat, bone, depth=.009):
    verts=[]
    for j,p in enumerate(pts):
        tangent=Vector(pts[min(j+1,len(pts)-1)])-Vector(pts[max(0,j-1)])
        across=Vector((tangent.z,0,-tangent.x)).normalized()*width/2
        for d in [-depth/2,depth/2]:
            verts += [tuple(Vector(p)-across+Vector((0,d,0))),tuple(Vector(p)+across+Vector((0,d,0)))]
    faces=[]
    for j in range(len(pts)-1):
        k=j*4;l=k+4
        faces += [(k,l,l+1,k+1),(k+2,k+3,l+3,l+2),(k,k+2,l+2,l),(k+1,l+1,l+3,k+3)]
    faces += [(0,1,3,2),(len(verts)-4,len(verts)-2,len(verts)-1,len(verts)-3)]
    return mesh(name,verts,faces,mat,bone)


def bone(name,head,tail,parent=None,deform=True):
    b=RIG.data.edit_bones.new(name); b.head=head;b.tail=tail;b.use_deform=deform
    b.align_roll(Vector((0,-1,0)))
    if parent:b.parent=RIG.data.edit_bones[parent]
    return b


def setup():
    global CHAR, RIG
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene=bpy.context.scene; scene.name='Windward - original adventurer'
    scene.unit_settings.system='METRIC';scene.unit_settings.scale_length=1
    scene.render.fps=30
    CHAR=bpy.data.collections.new('CHARACTER | skinned geometry');scene.collection.children.link(CHAR)
    for name,color,metal,rough in [
        ('Skin',(.69,.40,.23),0,.72),('SkinLight',(.87,.57,.34),0,.75),
        ('EarBlush',(.64,.27,.19),0,.8),('ForestCloth',(.075,.24,.14),0,.83),
        ('LeafCloth',(.19,.39,.20),0,.84),('Linen',(.82,.72,.49),0,.9),
        ('Stitch',(.69,.56,.29),0,.78),('Leather',(.18,.075,.033),0,.78),
        ('LeatherLight',(.29,.14,.062),0,.71),('Hair',(.57,.30,.058),0,.58),
        ('HairLight',(.84,.51,.13),0,.55),('HairShadow',(.30,.14,.03),0,.8),
        ('EyeWhite',(.96,.91,.76),0,.48),('Iris',(.075,.29,.29),.0,.42),
        ('Pupil',(.008,.025,.025),0,.5),('Ink',(.045,.027,.018),0,.8),
        ('Brass',(.60,.38,.11),.65,.36),('Steel',(.40,.58,.61),.7,.32),
        ('Edge',(.74,.84,.79),.7,.27),('Wood',(.31,.17,.08),0,.85),
        ('Ember',(.94,.40,.14),.1,.5)]: material(name,color,metal,rough)
    ad=bpy.data.armatures.new('Windward_Armature');RIG=bpy.data.objects.new('Windward_Rig',ad)
    CHAR.objects.link(RIG);RIG.show_in_front=True;RIG.data.display_type='OCTAHEDRAL'
    bpy.context.view_layer.objects.active=RIG;RIG.select_set(True)
    bpy.ops.object.mode_set(mode='EDIT')
    bone('root',(0,0,0),(0,0,.16))
    bone('pelvis',(0,0,.88),(0,0,1.01),'root')
    bone('spine_01',(0,0,1.01),(0,0,1.16),'pelvis')
    bone('spine_02',(0,0,1.16),(0,0,1.29),'spine_01')
    bone('spine_03',(0,0,1.29),(0,0,1.43),'spine_02')
    bone('neck_01',(0,0,1.43),(0,0,1.52),'spine_03')
    bone('head',(0,0,1.52),(0,0,1.82),'neck_01')
    for s,side in [(1,'l'),(-1,'r')]:
        shoulder=(s*.255,0,1.385);elbow=(s*.49,-.01,1.205);wrist=(s*.66,-.035,1.015)
        bone('clavicle_'+side,(s*.065,0,1.395),shoulder,'spine_03')
        bone('upperarm_'+side,shoulder,elbow,'clavicle_'+side)
        bone('lowerarm_'+side,elbow,wrist,'upperarm_'+side)
        hand_end=(s*.731,-.047,.942)
        bone('hand_'+side,wrist,hand_end,'lowerarm_'+side)
        # Four articulated fingers. Spread in depth across the palm.
        forward=(Vector(hand_end)-Vector(wrist)).normalized()
        for idx,(finger,offset,length) in enumerate([('index',-.036,.096),('middle',-.012,.108),('ring',.014,.098),('pinky',.036,.080)]):
            start=Vector(hand_end)+Vector((-s*.008,offset,.008))
            for j in range(3):
                end=start+forward*length/3
                n=f'{finger}_{j+1:02d}_{side}'
                bone(n,start,end,'hand_'+side if j==0 else f'{finger}_{j:02d}_{side}')
                start=end
        a=Vector(wrist)+Vector((s*.003,-.055,-.031))
        d=Vector((-s*.35,-.20,-.45)).normalized()
        for j in range(3):
            b=a+d*.025
            bone(f'thumb_{j+1:02d}_{side}',a,b,'hand_'+side if j==0 else f'thumb_{j:02d}_{side}');a=b
        hip=(s*.127,0,.91);knee=(s*.137,-.032,.515);ankle=(s*.143,0,.135)
        bone('thigh_'+side,hip,knee,'pelvis')
        bone('calf_'+side,knee,ankle,'thigh_'+side)
        bone('foot_'+side,ankle,(s*.143,-.16,.06),'calf_'+side)
        bone('ball_'+side,(s*.143,-.16,.06),(s*.143,-.255,.055),'foot_'+side)
        bone('CTRL_foot_'+side,ankle,(s*.143,-.16,.135),'root',False)
        bone('CTRL_knee_'+side,(s*.137,-.65,.51),(s*.137,-.65,.63),'root',False)
        bone('skirt_front_'+side,(s*.12,-.06,1.015),(s*.18,-.09,.73),'pelvis')
        bone('skirt_back_'+side,(s*.12,.08,1.015),(s*.18,.10,.73),'pelvis')
    bone('cape_01',(0,.12,1.405),(0,.20,1.16),'spine_03')
    bone('cape_02',(0,.20,1.16),(0,.24,.965),'cape_01')
    bone('weapon_r',(-.708,-.073,.966),(-.708,-.073,.846),'hand_r')
    bone('shield_l',(.553,-.044,1.14),(.553,-.044,1.26),'lowerarm_l')
    bpy.ops.object.mode_set(mode='OBJECT')
    deform=RIG.data.collections.new('Deform | body, fingers, cloth')
    controls=RIG.data.collections.new('Controls | optional leg IK')
    for b in RIG.data.bones:
        (deform if b.use_deform else controls).assign(b)
    for side in ['l','r']:
        c=RIG.pose.bones['calf_'+side].constraints.new('IK')
        c.name='Leg IK (enable influence to use controls)';c.target=RIG;c.subtarget='CTRL_foot_'+side
        c.pole_target=RIG;c.pole_subtarget='CTRL_knee_'+side;c.chain_count=2;c.influence=0
        c.pole_angle=0
    for pb in RIG.pose.bones:pb.rotation_mode='QUATERNION'
    RIG['usage']='Pose Mode: rotate deform bones for FK. Optional leg IK constraints default to 0; set influence=1 then move CTRL_foot/CTRL_knee. Demo actions use FK.'
    RIG['skeleton_compatibility']='Original rig. UE Manny/Quinn animations require a separate retarget setup.'


def body():
    # Torso quad rings have manually blended pelvis/spine/chest weights.
    z=[.945,1.005,1.075,1.16,1.26,1.34,1.405]
    rs=[(.174,.116),(.173,.122),(.174,.119),(.187,.121),(.214,.137),(.227,.131),(.179,.095)]
    ws=[{'pelvis':1},{'pelvis':.65,'spine_01':.35},{'spine_01':1},
        {'spine_01':.45,'spine_02':.55},{'spine_02':.7,'spine_03':.3},{'spine_03':1},{'spine_03':1}]
    loft('Tunic_Bodice',[(0,0,k) for k in z],rs,'ForestCloth',ws,24)
    loft('Neck',[(0,0,1.37),(0,0,1.435),(0,0,1.52)],[ (.066,.065),(.065,.062),(.070,.064)],'SkinLight',
         [{'spine_03':1},{'neck_01':1},{'head':.7,'neck_01':.3}],20)
    # Split short tunic, four panels with their own cloth bones.
    seg=32;verts=[];weights=[];faces=[]
    for j,(zz,rx,ry) in enumerate([(1.015,.184,.131),(.90,.215,.145),(.775,.25,.167),(.73,.257,.169)]):
        for i in range(seg):
            a=math.tau*i/seg;x=rx*math.cos(a);y=ry*math.sin(a)
            # Two side notches and a tailored diagonal hem.
            h=(.018*math.sin(a*2) + (.053 if abs(y)<.025 else 0)) if j==3 else 0
            verts.append((x,y,zz+h))
            side='l' if x>=0 else 'r';direction='front' if y<0 else 'back'
            w=j/3
            weights.append({'pelvis':1-w, f'skirt_{direction}_{side}':w} if j<3 else {f'skirt_{direction}_{side}':1})
    for j in range(3):
        faces += [(j*seg+i,j*seg+(i+1)%seg,(j+1)*seg+(i+1)%seg,(j+1)*seg+i) for i in range(seg)]
    ob=mesh('Tunic_Split_Hem',verts,faces,'LeafCloth',weights)
    sol=ob.modifiers.new('Cloth thickness','SOLIDIFY');sol.thickness=.008
    bpy.context.view_layer.objects.active=ob;bpy.ops.object.modifier_apply(modifier=sol.name)
    # Collar and laced placket sit on the chest.
    for s in [-1,1]:
        prism('Folded_Collar',[(s*.015,1.41),(s*.066,1.445),(s*.118,1.372),(s*.052,1.324)],-.095,.017,'Linen','spine_03',.004)
    box('Laced_Placket',(0,-.132,1.295),(.062,.016,.117),'Leather','spine_03',.004)
    for j in range(3):
        zz=1.26+j*.031
        ribbon('Chest_Lacing',[(-.025,-.145,zz),(.025,-.145,zz+.026)],.005,'Stitch','spine_03')
        ribbon('Chest_Lacing',[(.025,-.146,zz),(-.025,-.146,zz+.026)],.005,'Stitch','spine_03')
    # Wide belt, wrapped as oval rings.
    loft('Leather_Belt',[(0,0,1.008),(0,0,1.066)],[(.181,.128),(.179,.128)],'Leather','pelvis',32,axis=(0,0,1))
    box('Buckle',(0,-.136,1.036),(.078,.028,.061),'Brass','pelvis',.008)
    box('Buckle_Inset',(0,-.153,1.036),(.046,.012,.030),'Leather','pelvis',.002)
    box('Buckle_Pin',(0,-.162,1.036),(.006,.01,.037),'Brass','pelvis',.001)
    ribbon('Shoulder_Satchel_Strap',[(.145,-.107,1.397),(.079,-.145,1.303),(-.011,-.144,1.19),(-.125,-.131,1.061)],.036,'LeatherLight','spine_02')
    box('Strap_Brass_Slide',(.06,-.160,1.286),(.047,.021,.047),'Brass','spine_02',.004)
    ellipsoid('Hip_Pouch',(-.217,.008,.971),(.075,.072,.088),'LeatherLight','pelvis',8,16)
    box('Pouch_Flap',(-.224,-.058,.986),(.122,.025,.062),'Leather','pelvis',.013)
    ellipsoid('Pouch_Clasp',(-.224,-.078,.978),(.01,.005,.013),'Brass','pelvis',6,12)
    # A short asymmetrical shoulder cape, supported by two bones.
    verts=[];ww=[]
    for j,(zz,rx,yy) in enumerate([(1.40,.16,.10),(1.32,.24,.17),(1.16,.255,.203),(1.0,.245,.228),(.95,.215,.237)]):
        for i in range(9):
            t=(i/8-.5)*2;x=t*rx
            verts.append((x,yy+.046*(1-t*t),zz+.034*abs(t) if j==4 else zz))
            ww.append({'spine_03':1} if j==0 else {'cape_01':1} if j<3 else {'cape_01':.25,'cape_02':.75})
    faces=[(j*9+i,j*9+i+1,(j+1)*9+i+1,(j+1)*9+i) for j in range(4) for i in range(8)]
    ob=mesh('Short_Travel_Cape',verts,faces,'ForestCloth',ww)
    bpy.context.view_layer.objects.active=ob;m=ob.modifiers.new('Cape thickness','SOLIDIFY');m.thickness=.009;bpy.ops.object.modifier_apply(modifier=m.name)
    ellipsoid('Cape_Brooch',(.154,-.115,1.397),(.032,.012,.03),'Brass','spine_03',8,16)
    prism('Brooch_Leaf',[(.139,1.394),(.155,1.421),(.172,1.396),(.152,1.38)],-.129,.005,'LeafCloth','spine_03')


def limbs():
    for s,side in [(1,'l'),(-1,'r')]:
        upper='upperarm_'+side;lower='lowerarm_'+side;hand='hand_'+side
        shoulder=Vector((s*.255,0,1.385));elbow=Vector((s*.49,-.01,1.205));wrist=Vector((s*.66,-.035,1.015))
        arm=[shoulder,shoulder.lerp(elbow,.3),shoulder.lerp(elbow,.74),elbow,elbow.lerp(wrist,.16),elbow.lerp(wrist,.7),wrist]
        loft('Arm_'+side,arm,[(.070,.065),(.066,.06),(.054,.052),(.049,.048),(.049,.049),(.043,.041),(.033,.034)],'SkinLight',
             [{upper:1},{upper:1},{upper:1},{upper:.5,lower:.5},{lower:1},{lower:1},{lower:.7,hand:.3}],16)
        sleeve=[Vector((s*.176,0,1.373)),shoulder,shoulder.lerp(elbow,.26),shoulder.lerp(elbow,.57)]
        loft('Tunic_Sleeve_'+side,sleeve,[(.071,.076),(.079,.076),(.083,.076),(.068,.066)],'LeafCloth',
             [{'spine_03':1},{'spine_03':.35,upper:.65},{upper:1},{upper:1}],20)
        c=shoulder.lerp(elbow,.57)
        loft('Sleeve_Edge_'+side,[c,c+(elbow-shoulder).normalized()*.022],[(.069,.067),(.068,.066)],'Linen',upper,20)
        loft('Leather_Bracer_'+side,[elbow.lerp(wrist,.48),elbow.lerp(wrist,.73),wrist],[(.052,.05),(.051,.048),(.04,.039)],'LeatherLight',lower,16)
        for t in [.52,.86]:
            c=elbow.lerp(wrist,t);d=(wrist-elbow).normalized()
            loft('Bracer_Band_'+side,[c-d*.009,c+d*.009],[(.053,.051)]*2,'Leather',lower,16)
        end=Vector((s*.731,-.047,.942));d=(end-wrist).normalized()
        loft('Palm_'+side,[wrist,wrist.lerp(end,.65),end],[(.033,.035),(.034,.045),(.031,.045)],'SkinLight',hand,16)
        # Finger ring positions follow the corresponding editable bone chain.
        for f in ['index','middle','ring','pinky','thumb']:
            names=[f'{f}_{j:02d}_{side}' for j in range(1,4)]
            bones=[RIG.data.bones[n] for n in names]
            centers=[];weights=[];radii=[]
            radius=.0085 if f=='pinky' else .0105 if f=='thumb' else .0093
            for j,b in enumerate(bones):
                centers.append(b.head_local);radii.append((radius*(1-.10*j),radius*(1-.10*j)))
                weights.append({names[j]:1})
                centers.append(b.head_local.lerp(b.tail_local,.55));radii.append((radius*(1-.13*j),)*2);weights.append({names[j]:1})
            centers.append(bones[-1].tail_local);radii.append((radius*.57,)*2);weights.append({names[-1]:1})
            loft(f.capitalize()+'_'+side,centers,radii,'SkinLight',weights,8)
        # Pants bend with the hip and knee. Boots retain a smooth ankle blend.
        hip=Vector((s*.127,0,.91));knee=Vector((s*.137,-.032,.515));ankle=Vector((s*.143,0,.135))
        thigh='thigh_'+side;calf='calf_'+side;foot='foot_'+side
        loft('Linen_Trousers_'+side,[hip,hip.lerp(knee,.34),hip.lerp(knee,.80),knee,knee.lerp(ankle,.16),knee.lerp(ankle,.38)],
             [(.087,.085),(.080,.079),(.066,.066),(.06,.06),(.062,.061),(.061,.06)],'Linen',
             [{thigh:1},{thigh:1},{thigh:1},{thigh:.5,calf:.5},{calf:1},{calf:1}],20)
        loft('Tall_Boot_'+side,[(s*.143,0,.10),(s*.142,0,.17),(s*.14,-.01,.285),(s*.14,-.02,.388)],
             [(.065,.070),(.060,.062),(.064,.070),(.072,.072)],'LeatherLight',
             [{foot:.65,calf:.35},{calf:.85,foot:.15},{calf:1},{calf:1}],20,axis=(0,0,1))
        loft('Boot_Cuff_'+side,[(s*.14,-.02,.373),(s*.14,-.02,.414)],[(.078,.078),(.077,.077)],'Leather',calf,20,axis=(0,0,1))
        ellipsoid('Boot_Foot_'+side,(s*.143,-.085,.079),(.071,.173,.080),'LeatherLight',foot,8,20)
        ellipsoid('Boot_Sole_'+side,(s*.143,-.083,.030),(.073,.178,.025),'Leather',foot,6,20)
        for zz in [.207,.335]:
            loft('Boot_Strap_'+side,[(s*.141,-.01,zz-.01),(s*.141,-.01,zz+.01)],[(.068,.073)]*2,'Leather',calf,20,axis=(0,0,1))
            box('Boot_Buckle_'+side,(s*.18,-.073,zz),(.025,.015,.026),'Brass',calf,.003)


def face_and_hair():
    # Custom face loops: youthful jaw, broad cheekbones, tapering chin.
    zs=[1.475,1.50,1.55,1.60,1.66,1.73,1.795,1.84,1.86]
    xs=[.05,.090,.127,.157,.163,.158,.139,.085,.010]
    ys=[.07,.098,.116,.129,.135,.133,.118,.07,.010]
    centers=[(0,-.014,z) for z in zs]
    loft('Face_Head',centers,list(zip(xs,ys)),'SkinLight','head',32,axis=(0,0,1))
    # Ears are tapered pointed shells, each with an inset inner leaf.
    for s in [-1,1]:
        outline=[(s*.133,1.657),(s*.190,1.697),(s*.273,1.737),(s*.230,1.64),(s*.173,1.597),(s*.14,1.607)]
        prism('Pointed_Ear',outline,-.006,.053,'SkinLight','head',.008)
        prism('Ear_Inner',[(s*.158,1.651),(s*.245,1.718),(s*.205,1.644),(s*.169,1.617)],-.035,.006,'EarBlush','head',.003)
        # Almond eyes with a dark silhouette, iris and a tiny catchlight.
        ellipsoid('Eye_Outline',(s*.068,-.136,1.665),(.045,.012,.029),'Ink','head',8,20)
        ellipsoid('Eye_White',(s*.068,-.146,1.667),(.039,.010,.023),'EyeWhite','head',10,24)
        ellipsoid('Iris',(s*.066,-.155,1.666),(.016,.006,.021),'Iris','head',10,20)
        ellipsoid('Pupil',(s*.064,-.161,1.666),(.0075,.003,.015),'Pupil','head',8,16)
        ellipsoid('Eye_Glint',(s*.06-.004,-.164,1.675),(.004,.002,.005),'EyeWhite','head',6,12)
        ribbon('Upper_Eyelid',[(s*.029,-.15,1.675),(s*.058,-.157,1.690),(s*.098,-.143,1.68)],.005,'HairShadow','head',.003)
        ribbon('Eyebrow',[(s*.030,-.137,1.706),(s*.058,-.141,1.719),(s*.103,-.128,1.711)],.012,'HairShadow','head',.005)
    # Small wedge nose, restrained mouth and lip highlight.
    mesh('Sculpted_Nose',[(-.012,-.13,1.666),(.012,-.13,1.666),(-.019,-.143,1.624),(.019,-.143,1.624),(0,-.176,1.629),(0,-.137,1.619)],
         [(0,1,4),(0,4,2),(1,3,4),(2,4,5),(4,3,5),(0,2,5,3,1)],'SkinLight','head')
    ribbon('Mouth',[(-.029,-.131,1.584),(0,-.139,1.581),(.025,-.132,1.588)],.0035,'EarBlush','head',.002)
    ribbon('Lower_Lip',[(-.016,-.134,1.574),(0,-.137,1.572),(.017,-.134,1.577)],.003,'Skin','head',.002)
    # Hair cap, open near brows, with hand-authored wedge locks.
    verts=[];faces=[];seg=40
    for j in range(8):
        t=j/7
        for i in range(seg):
            a=math.tau*i/seg
            # Front points -Y. Back cap extends down to the neck.
            bottom=1.68 if math.sin(a)<-.1 else 1.57
            phi=.04+t*(math.acos(max(-1,min(1,(bottom-1.716)/.167)))-.04)
            verts.append((.169*math.sin(phi)*math.cos(a),.012+.149*math.sin(phi)*math.sin(a),1.716+.168*math.cos(phi)))
    for j in range(7):
        faces += [(j*seg+i,j*seg+(i+1)%seg,(j+1)*seg+(i+1)%seg,(j+1)*seg+i) for i in range(seg)]
    faces.append(tuple(reversed(range(seg))))
    mesh('Hair_Cap',verts,faces,'Hair','head')
    locks=[
        ([(-.115,-.071,1.827),(-.04,-.108,1.846),(-.039,-.164,1.738),(-.092,-.163,1.696),(-.123,-.138,1.745)],'HairLight'),
        ([(-.045,-.086,1.853),(.043,-.086,1.844),(.055,-.145,1.748),(.005,-.172,1.698),(-.025,-.155,1.753)],'HairLight'),
        ([(.031,-.073,1.847),(.107,-.065,1.825),(.139,-.106,1.749),(.148,-.140,1.680),(.077,-.158,1.743)],'Hair'),
        ([(-.150,-.03,1.785),(-.105,-.101,1.78),(-.117,-.133,1.606),(-.161,-.089,1.625)],'Hair'),
        ([(.106,-.078,1.792),(.159,-.016,1.76),(.174,-.072,1.606),(.128,-.111,1.628)],'HairLight'),
        ([(-.087,.11,1.803),(-.159,.05,1.766),(-.20,.07,1.678),(-.134,.13,1.66)],'HairLight'),
        ([(.112,.11,1.793),(.162,.047,1.766),(.20,.083,1.675),(.133,.142,1.65)],'Hair'),
        ([(-.10,.13,1.718),(-.033,.161,1.71),(-.084,.157,1.536),(-.133,.116,1.574)],'Hair'),
        ([(-.035,.156,1.736),(.054,.153,1.74),(.042,.168,1.532),(-.026,.18,1.558)],'HairLight'),
        ([(.048,.147,1.726),(.129,.11,1.72),(.143,.14,1.568),(.09,.175,1.53)],'Hair'),
    ]
    for k,(outline,mat) in enumerate(locks):
        # Convex ridge changes the highlight along each hair lock.
        n=len(outline)
        c=sum((Vector(p) for p in outline),Vector())/n
        normal=Vector((c.x,c.y-.008,.1)).normalized()
        verts=outline+[tuple(c+normal*.020)]
        mesh('Hair_Lock_%02d'%k,verts,[(i,(i+1)%n,n) for i in range(n)]+[tuple(reversed(range(n)))],mat,'head',False)
    # Small original leaf knot keeps the silhouette distinct from an existing hero.
    ribbon('Hair_Tie',[(-.047,.143,1.615),(.0,.17,1.601),(.047,.143,1.615)],.014,'ForestCloth','head')


def equipment():
    # Sword kept separate so artists can hide or reparent it.
    x=-.740;y=-.017;z=.900
    blade=[(x-.019,z-.04),(x+.019,z-.04),(x+.026,z-.47),(x,z-.58),(x-.026,z-.47)]
    ob=prism('Sword_Blade',blade,y,.017,'Steel','weapon_r')
    prism('Sword_Fuller',[(x-.005,z-.075),(x+.005,z-.075),(x+.006,z-.47),(x,z-.53),(x-.006,z-.47)],y-.011,.003,'Edge','weapon_r')
    box('Sword_Guard',(x,y,z-.026),(.155,.032,.030),'Brass','weapon_r',.011)
    loft('Sword_Grip',[(x,y,z-.01),(x,y,z+.083)],[(.017,.017)]*2,'Leather','weapon_r',12,axis=(0,0,1))
    ellipsoid('Sword_Pommel',(x,y,z+.088),(.022,.021,.027),'Brass','weapon_r',8,16)
    for j in range(4):
        box('Grip_Wrap',(x,y-.017,z+.008+j*.018),(.028,.007,.005),'LeatherLight','weapon_r',.001)
    # Kite shield with an original leaf/ember motif, no franchise emblems.
    cx=.61;cy=-.139;cz=1.148
    outline=[(cx-.132,cz+.143),(cx,cz+.183),(cx+.132,cz+.143),(cx+.139,cz-.060),(cx,cz-.208),(cx-.139,cz-.060)]
    prism('Shield_Rim',outline,cy,.045,'Brass','shield_l',.008)
    inner=[(cx+(x-cx)*.88,cz+(z-cz)*.88) for x,z in outline]
    prism('Shield_Face',inner,cy-.028,.023,'ForestCloth','shield_l',.004)
    prism('Shield_Leaf',[(cx-.063,cz-.03),(cx,cz+.11),(cx+.071,cz+.024),(cx+.004,cz-.092)],cy-.044,.006,'LeafCloth','shield_l',.002)
    ribbon('Leaf_Vein',[(cx-.012,cy-.05,cz-.093),(cx+.010,cy-.05,cz+.099)],.008,'Brass','shield_l')
    ellipsoid('Shield_Ember',(cx,cy-.055,cz-.125),(.015,.005,.018),'Ember','shield_l',8,12)
    for x,z in outline:
        ellipsoid('Shield_Rivet',(cx+(x-cx)*.92,cy-.028,cz+(z-cz)*.92),(.006,.006,.006),'Brass','shield_l',6,12)


def skin_and_uv():
    bpy.ops.object.select_all(action='DESELECT')
    for ob in PARTS:
        ob.select_set(True)
    bpy.context.view_layer.objects.active=PARTS[0]
    bpy.ops.object.mode_set(mode='EDIT');bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.smart_project(angle_limit=math.radians(66),island_margin=.018)
    bpy.ops.object.mode_set(mode='OBJECT')
    for ob in PARTS:
        ob.parent=RIG
        mod=ob.modifiers.new('Windward skin','ARMATURE');mod.object=RIG;mod.use_deform_preserve_volume=True
        ob['authoring']='Original explicit mesh construction + hand-assigned skin weights'
    # Combine sections into five sensible editable meshes, preserving material slots/groups.
    buckets={'Windward_Body':[],'Windward_Head':[],'Windward_Hair':[],'Windward_Sword':[],'Windward_Shield':[]}
    for ob in PARTS:
        key='Windward_Sword' if ob.name.startswith(('Sword_','Grip_')) else 'Windward_Shield' if ob.name.startswith(('Shield_','Leaf_Vein')) else 'Windward_Hair' if ob.name.startswith('Hair_') else 'Windward_Head' if set(g.name for g in ob.vertex_groups)=={'head'} else 'Windward_Body'
        buckets[key].append(ob)
    PARTS.clear()
    for name,obs in buckets.items():
        bpy.ops.object.select_all(action='DESELECT')
        for ob in obs:ob.select_set(True)
        bpy.context.view_layer.objects.active=obs[0]
        bpy.ops.object.join();ob=bpy.context.object;ob.name=name
        # Joined objects retain one armature modifier, explicit group weights and materials.
        bpy.context.scene.cursor.location=(0,0,0);bpy.ops.object.origin_set(type='ORIGIN_CURSOR')
        bpy.ops.object.transform_apply(location=False,rotation=True,scale=True)
        for m in list(ob.modifiers):
            if m.type=='ARMATURE' and m != ob.modifiers[0]:ob.modifiers.remove(m)
        PARTS.append(ob)


def reset_pose():
    for pb in RIG.pose.bones:
        pb.location=(0,0,0);pb.rotation_quaternion=(1,0,0,0);pb.scale=(1,1,1)


def turn(name,axis,degrees):
    pb=RIG.pose.bones[name]
    rest=pb.bone.matrix_local.to_quaternion()
    pb.rotation_quaternion=rest.inverted() @ Quaternion(Vector(axis),math.radians(degrees)) @ rest


def relaxed():
    # Lower A-pose arms to a comfortable stance.
    turn('upperarm_l',(0,1,0),23)
    turn('upperarm_r',(0,1,0),-23)
    turn('lowerarm_l',(1,0,0),-8)
    turn('lowerarm_r',(1,0,0),-8)
    for side in ['l','r']:
        for f in ['index','middle','ring','pinky']:
            b=RIG.data.bones[f'{f}_01_{side}']
            curl_axis=(b.tail_local-b.head_local).normalized().cross(Vector((0,1,0))).normalized()
            for j,angle in [(1,48),(2,56),(3,34)]:turn(f'{f}_{j:02d}_{side}',curl_axis,angle)
        turn('thumb_01_'+side,(0,0,1),12 if side=='l' else -12)


def actions():
    RIG.animation_data_create()
    specs=[('Idle_Breathe',61),('Walk_InPlace',31),('Wave_Hello',81),('Sword_Slash',41)]
    for name,end in specs:
        act=bpy.data.actions.new(name);act.use_fake_user=True;RIG.animation_data.action=act
        act['purpose']='Editable FK demo; prototype animation, not gameplay acceptance'
        for frame in range(1,end+1,2):
            t=(frame-1)/(end-1);phase=t*math.tau
            reset_pose();relaxed()
            if name=='Idle_Breathe':
                turn('spine_02',(1,0,0),1.8*math.sin(phase));turn('head',(0,0,1),2*math.sin(phase))
                turn('cape_01',(1,0,0),2*math.sin(phase+.6))
            elif name=='Walk_InPlace':
                for s,side in [(1,'l'),(-1,'r')]:
                    ph=phase+(math.pi if s<0 else 0)
                    turn('thigh_'+side,(1,0,0),26*math.cos(ph))
                    turn('calf_'+side,(1,0,0),-32*max(0,math.sin(ph)))
                    turn('foot_'+side,(1,0,0),-10*math.cos(ph))
                    turn('upperarm_'+side,(0,1,0),23*s)
                    # A second local quaternion adds the forward/back arm swing.
                    pb=RIG.pose.bones['upperarm_'+side]
                    rest=pb.bone.matrix_local.to_quaternion()
                    pb.rotation_quaternion=pb.rotation_quaternion @ (rest.inverted()@Quaternion((1,0,0),math.radians(-13*math.cos(ph)))@rest)
                    turn('skirt_front_'+side,(1,0,0),16*max(0,math.cos(ph)))
                    turn('skirt_back_'+side,(1,0,0),-13*max(0,-math.cos(ph)))
                RIG.pose.bones['pelvis'].location.y=.012*(1-math.cos(2*phase))
                turn('spine_02',(0,0,1),3*math.sin(phase))
                turn('cape_02',(1,0,0),5*math.sin(phase))
            elif name=='Wave_Hello':
                lift=math.sin(math.pi*min(1,t/.24)/2) if t<.24 else math.sin(math.pi*min(1,(1-t)/.24)/2) if t>.76 else 1
                turn('upperarm_r',(0,1,0),63*lift-23*(1-lift))
                turn('lowerarm_r',(0,1,0),78*lift)
                turn('hand_r',(0,1,0),12*math.sin(phase*3)*lift)
                for f in ['index','middle','ring','pinky']:
                    for j in range(1,4):turn(f'{f}_{j:02d}_r',(1,0,0),6*(1-lift))
                turn('head',(0,1,0),-5*lift)
            elif name=='Sword_Slash':
                # Anticipation, release, follow-through and recover.
                def lerpkeys(keys):
                    for (a,v),(b,w) in zip(keys,keys[1:]):
                        if a<=t<=b:
                            q=(t-a)/(b-a);q=q*q*(3-2*q);return v+(w-v)*q
                    return keys[-1][1]
                swing=lerpkeys([(0,0),(.35,-48),(.52,72),(.68,52),(1,0)])
                turn('spine_02',(0,0,1),swing*.25)
                turn('upperarm_r',(1,0,0),swing)
                turn('lowerarm_r',(0,1,0),lerpkeys([(0,0),(.35,55),(.52,8),(.68,14),(1,0)]))
                turn('upperarm_l',(0,1,0),26)
                turn('cape_01',(0,0,1),-swing*.10)
            for pb in RIG.pose.bones:
                if not pb.bone.use_deform:continue
                pb.keyframe_insert(data_path='rotation_quaternion',frame=frame,group=pb.name)
                if pb.name=='pelvis':pb.keyframe_insert(data_path='location',frame=frame,group=pb.name)
        print('ACTION',name,end,flush=True)
    RIG.animation_data.action=bpy.data.actions['Idle_Breathe']
    bpy.context.scene.frame_start=1;bpy.context.scene.frame_end=61;bpy.context.scene.frame_set(1)


def presentation():
    scene=bpy.context.scene
    studio=bpy.data.collections.new('STUDIO | excluded from exports');scene.collection.children.link(studio)
    def to_studio(ob):
        for c in list(ob.users_collection):c.objects.unlink(ob)
        studio.objects.link(ob)
    material('Backdrop',(.024,.043,.043),0,.94)
    material('Plinth',(.09,.13,.115),.12,.76)
    bpy.ops.mesh.primitive_plane_add(size=200)
    floor=bpy.context.object;floor.name='Studio_Floor';floor.data.materials.append(MATS['Backdrop']);floor.location.z=-.075;to_studio(floor)
    bpy.ops.mesh.primitive_cylinder_add(vertices=96,radius=.66,depth=.065,location=(0,0,-.0325))
    podium=bpy.context.object;podium.name='Display_Plinth';podium.data.materials.append(MATS['Plinth']);to_studio(podium)
    bevel=podium.modifiers.new('Rounded edge','BEVEL');bevel.width=.018;bevel.segments=3
    for poly in podium.data.polygons:poly.use_smooth=True
    def area(name,loc,energy,size,color):
        data=bpy.data.lights.new(name,'AREA');data.energy=energy;data.shape='DISK';data.size=size;data.color=color
        ob=bpy.data.objects.new(name,data);studio.objects.link(ob);ob.location=loc
        ob.rotation_euler=(Vector((0,0,1.05))-ob.location).to_track_quat('-Z','Y').to_euler()
    area('Key softbox',(-3,-4,5),470,3.8,(1,.80,.60))
    area('Cool fill',(3,-2,2.8),270,3,(.61,.84,1))
    area('Golden rim',(1,3,4),650,2.5,(1,.78,.45))
    world=bpy.data.worlds.new('Windward studio');world.use_nodes=True
    world.node_tree.nodes['Background'].inputs[0].default_value=(.11,.17,.19,1)
    world.node_tree.nodes['Background'].inputs[1].default_value=.35;scene.world=world
    camdata=bpy.data.cameras.new('Hero_Camera');cam=bpy.data.objects.new('Hero_Camera',camdata);studio.objects.link(cam)
    cam.location=(2.5,-6,2.75);target=Vector((0,0,.99));cam.rotation_euler=(target-cam.location).to_track_quat('-Z','Y').to_euler()
    camdata.type='ORTHO';camdata.ortho_scale=2.52;scene.camera=cam
    scene.render.engine='CYCLES';scene.cycles.samples=48;scene.cycles.use_denoising=True
    scene.render.resolution_x=1100;scene.render.resolution_y=1100;scene.render.resolution_percentage=100
    scene.view_settings.view_transform='AgX'
    scene.render.image_settings.file_format='PNG'
    # Viewport is usable immediately on opening the blend.
    from mathutils import Euler
    for screen in bpy.data.screens:
        for area in screen.areas:
            if area.type=='VIEW_3D':
                area.spaces.active.region_3d.view_distance=3.1
                area.spaces.active.region_3d.view_location=(0,0,1)
                area.spaces.active.region_3d.view_rotation=Euler((math.radians(78),0,math.radians(16))).to_quaternion()
                area.spaces.active.shading.type='MATERIAL'
                area.spaces.active.overlay.show_floor=False
    bpy.ops.object.select_all(action='DESELECT');RIG.select_set(True);bpy.context.view_layer.objects.active=RIG
    # Keep studio out of ordinary viewport editing, still available for render.
    studio.hide_viewport=True
    return cam,studio


def export_and_save():
    scene=bpy.context.scene
    bpy.ops.object.select_all(action='DESELECT')
    RIG.select_set(True)
    for ob in PARTS:ob.select_set(True)
    bpy.context.view_layer.objects.active=RIG
    current=RIG.animation_data.action
    RIG.animation_data.action=None;reset_pose();bpy.context.view_layer.update()
    kwargs=dict(use_selection=True,object_types={'MESH','ARMATURE'},add_leaf_bones=False,
                axis_forward='-Y',axis_up='Z',apply_unit_scale=True,apply_scale_options='FBX_SCALE_UNITS',
                use_armature_deform_only=True,use_custom_props=True)
    bpy.ops.export_scene.fbx(filepath=str(OUT/'Exports'/'SK_WindwardHero.fbx'),bake_anim=False,**kwargs)
    # Each action gets its own FBX, avoiding ambiguous take selection in engines.
    for name,end in [('Idle_Breathe',61),('Walk_InPlace',31),('Wave_Hello',81),('Sword_Slash',41)]:
        RIG.animation_data.action=bpy.data.actions[name]
        scene.frame_start=1;scene.frame_end=end;scene.frame_set(1)
        bpy.ops.export_scene.fbx(filepath=str(OUT/'Exports'/f'AN_WindwardHero_{name}.fbx'),
            bake_anim=True,bake_anim_use_all_actions=False,bake_anim_use_nla_strips=False,
            bake_anim_use_all_bones=True,bake_anim_force_startend_keying=True,bake_anim_simplify_factor=0,**kwargs)
    RIG.animation_data.action=current;scene.frame_start=1;scene.frame_end=61;scene.frame_set(1)
    bpy.ops.export_scene.gltf(filepath=str(OUT/'Exports'/'WindwardHero.glb'),use_selection=True,
        export_format='GLB',export_animations=True,export_animation_mode='ACTIONS',export_skins=True)
    notes=bpy.data.texts.new('READ_ME - rig and prototype scope')
    notes.write('WINDWARD / 风行者\nOriginal stylized adventurer, hand-authored Blender geometry.\n\nPose Mode: FK body and all fingers. Optional calf IK constraint influence defaults to zero.\nFour demo actions at 30 fps: Idle_Breathe, Walk_InPlace, Wave_Hello, Sword_Slash.\nHide Sword and Shield for the empty-hand wave. All actions editable.\nCloth and cape have explicit bones; no cloth simulation.\nCustom skeleton: requires retargeting for Manny/Quinn. No face rig.\nPrototype only, not a production/gameplay acceptance. See README.zh-CN.md.\n')
    bpy.context.preferences.filepaths.save_version=0
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'WindwardHero.blend'))
    # Export descriptive facts only; no test or acceptance claims.
    stats=[]
    for ob in PARTS:
        ob.data.calc_loop_triangles()
        stats.append({'mesh':ob.name,'vertices':len(ob.data.vertices),'triangles':len(ob.data.loop_triangles),'uv_layers':len(ob.data.uv_layers)})
    info={'blender':bpy.app.version_string,'height_m':1.884,'authoring':'Explicit original geometry and skin weights, no external meshes',
          'bones':len(RIG.data.bones),'deform_bones':sum(b.use_deform for b in RIG.data.bones),
          'actions':['Idle_Breathe','Walk_InPlace','Wave_Hello','Sword_Slash'],'fps':30,'meshes':stats,
          'status':'Art experiment. UE not compiled or tested. Not imported into runtime.',
          'limitations':['No facial rig','No cloth simulation','Original skeleton requires UE retarget setup','Demo walk is in-place; foot lock is not production polished']}
    (OUT/'asset_manifest.json').write_text(json.dumps(info,ensure_ascii=False,indent=2),encoding='utf-8')
    print('SAVED',OUT,flush=True)


def render_previews(cam,studio):
    scene=bpy.context.scene
    studio.hide_viewport=False
    scene.render.filepath=str(OUT/'Previews'/'Hero_ThreeQuarter.png')
    bpy.ops.render.render(write_still=True)
    # Clear front and back render to inspect silhouette and clothing.
    for label,loc in [('Hero_Front',(0,-6,1.9)),('Hero_Back',(0,6,2.1))]:
        cam.location=loc;cam.rotation_euler=(Vector((0,0,.99))-cam.location).to_track_quat('-Z','Y').to_euler()
        scene.render.filepath=str(OUT/'Previews'/f'{label}.png');bpy.ops.render.render(write_still=True)
    cam.location=(2.5,-6,2.75);cam.rotation_euler=(Vector((0,0,.99))-cam.location).to_track_quat('-Z','Y').to_euler()
    studio.hide_viewport=True
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'WindwardHero.blend'))


if __name__=='__main__':
    setup();print('BUILD torso',flush=True)
    body();print('BUILD limbs',flush=True)
    limbs();print('BUILD head',flush=True)
    face_and_hair();equipment();print('BUILD skin/UV',flush=True)
    skin_and_uv();actions();cam,studio=presentation();export_and_save()
    if '--no-render' not in sys.argv:render_previews(cam,studio)
