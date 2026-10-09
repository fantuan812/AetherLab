"""Read-only inputs, derived CHR01 + KITE01 static space study. No UE or animation acceptance."""
import bpy, math, json, hashlib, struct
from pathlib import Path
from mathutils import Vector, Matrix, Quaternion
ROOT=Path(__file__).resolve().parents[1]
INPUTS=ROOT.parent/'character_inputs'
CHAR=INPUTS/'stage1/source/CHR01_Staff_Grip.blend'
GUN=ROOT/'KITE01_Modular.blend'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
inputs={str(p.relative_to(ROOT.parent)):sha(p) for p in [CHAR,GUN]}
cal=json.loads((INPUTS/'stage1/reports/CHR01_Grip32_SourceCalibration.json').read_text())
anatomy=cal['anatomy']
bpy.ops.wm.open_mainfile(filepath=str(CHAR))
scene=bpy.context.scene;scene.frame_set(1)
arm=bpy.data.objects['CHR01_Wanderer_Rig65']
def rest_record():
 return {b.name:{'parent':b.parent.name if b.parent else None,'matrix_local':[list(r) for r in b.matrix_local],'head':list(b.head_local),'tail':list(b.tail_local)} for b in arm.data.bones}
def digest(d):return hashlib.sha256(json.dumps(d,sort_keys=True,separators=(',',':')).encode()).hexdigest()
rest_before=digest(rest_record())
character_meshes=[o for o in bpy.data.objects if o.type=='MESH' and any(c.name.startswith(('LOW','CONTROL')) for c in o.users_collection)]
def mesh_record(o):
 return digest({'vertices':[list(v.co) for v in o.data.vertices],'faces':[list(p.vertices) for p in o.data.polygons],'weights':[[[w.group,w.weight] for w in v.groups] for v in o.data.vertices],'groups':[g.name for g in o.vertex_groups]})
mesh_before={o.name:mesh_record(o) for o in character_meshes}
for im in bpy.data.images:
 if im.filepath and not im.packed_file:im.filepath=bpy.path.abspath(im.filepath)
# Keep the character and its rig only; all old weapon/studio objects removed from this derived scene.
keep={o.name for o in character_meshes}|{arm.name}
for o in list(bpy.data.objects):
 if o.name not in keep:bpy.data.objects.remove(o,do_unlink=True)
arm.animation_data_clear()
for pb in arm.pose.bones:
 pb.matrix_basis=Matrix.Identity(4);pb.rotation_mode='QUATERNION'
for o in character_meshes:
 if o.data.shape_keys:
  for fc in o.data.shape_keys.animation_data.drivers if o.data.shape_keys.animation_data else []:fc.mute=True
  for k in o.data.shape_keys.key_blocks:
   if k.name!='Basis':k.value=0
for c in bpy.data.collections:
 if c.name.startswith('CONTROL'):c.hide_render=True;c.hide_viewport=True
 if c.name.startswith('LOW'):c.hide_render=False;c.hide_viewport=False
# Import only LOD0 body objects and sockets from the standalone gun source.
with bpy.data.libraries.load(str(GUN),link=False) as (src,dst):
 dst.objects=[n for n in src.objects if n=='KITE01_Root' or n.startswith('SOCKET_KITE01_') or (n.startswith('SM_KITE01_') and '_LOD' not in n and n not in ['SM_KITE01_Stock_Compact','SM_KITE01_Muzzle_Cover'])]
guncol=bpy.data.collections.new('FIT_STUDY | KITE01 original LOD0');scene.collection.children.link(guncol)
for o in dst.objects:
 if o:guncol.objects.link(o);o.hide_render=False;o.hide_set(False)
scene.frame_set(1);bpy.context.view_layer.update()
for o in dst.objects:
 if o:o.animation_data_clear()
root=bpy.data.objects['KITE01_Root']
# Across-body low-ready geometry study, not a firing/aim pose.
angle=math.radians(0)
W=Matrix.Rotation(angle,4,'Z');W.translation=Vector((0,-.29,1.15));root.matrix_world=W
# Right palm closes round grip axis; left is a looser under-pad support study.
for n,q in cal['finger_pose_quaternion_wxyz'].items():
 if n in arm.pose.bones:
  quat=Quaternion(q)
  if n.endswith('_l'):quat=Quaternion().slerp(quat,.55)
  arm.pose.bones[n].rotation_quaternion=quat

def contact_frame(c,distal,axis):
 z=Vector(axis).normalized();x=Vector(distal);x=(x-z*x.dot(z)).normalized();y=z.cross(x)
 m=Matrix((x,y,z)).transposed().to_4x4();m.translation=Vector(c);return m
# Desired hand targets derived from measured CHR01 contact-to-hand transform.
Cr=contact_frame((-.128,0,-.082),(1,0,-.42),(.38,0,.925))
Cl=contact_frame((.30,0,-.012),(0,-1,0),(1,0,0))
contacts={'r':W@Cr,'l':W@Cl}

def solve(side,H,pole):
 g=anatomy[side];d=Vector(g['distal']);a0=Vector(g['thumb_side']);n0=Vector(g['palm']);basis=Matrix((d,a0,n0)).transposed()
 u=arm.data.bones['upperarm_'+side];l=arm.data.bones['lowerarm_'+side];h=arm.data.bones['hand_'+side]
 S=u.head_local;V=H.translation;U=(l.head_local-S).length;L=(h.head_local-l.head_local).length
 delta=V-S;D=delta.length;direction=delta.normalized();x=(U*U-L*L+D*D)/(2*D);radius2=U*U-x*x
 if radius2<0:raise ValueError('Hand unreachable: '+str((side,D,U+L)))
 center=S+direction*x;Rd=H.to_3x3()@h.matrix_local.to_3x3().inverted()
 want=V-L*(Rd@d);v=want-center;v-=direction*v.dot(direction)
 if v.length<1e-6:v=Vector(pole)-center;v-=direction*v.dot(direction)
 E=center+v.normalized()*math.sqrt(radius2);f=(V-E).normalized();a=Rd@a0;a=(a-f*a.dot(f)).normalized();n=g['sign']*f.cross(a)
 R=Matrix((f,a,n)).transposed()@basis.inverted();Ur=(l.head_local-u.head_local).normalized().rotation_difference((E-S).normalized()).to_matrix()
 up=Ur.to_4x4()@u.matrix_local;up.translation=S;lp=R.to_4x4()@l.matrix_local;lp.translation=E
 pp=arm.pose.bones[u.parent.name].matrix
 arm.pose.bones[u.name].matrix_basis=u.matrix_local.inverted()@u.parent.matrix_local@pp.inverted()@up
 arm.pose.bones[l.name].matrix_basis=l.matrix_local.inverted()@u.matrix_local@up.inverted()@lp
 arm.pose.bones[h.name].matrix_basis=h.matrix_local.inverted()@l.matrix_local@lp.inverted()@H
 return {'elbow':list(E),'wrist_basis_angle_deg':math.degrees(arm.pose.bones[h.name].matrix_basis.to_quaternion().angle),'wrist_target':list(V),'reach_m':D,'two_bone_length_m':U+L}
fit={}
for side in ['r','l']:
 H=contacts[side]@Matrix(anatomy[side]['contact_frame_hand_local']).inverted()
 fit[side]=solve(side,H,(-.30 if side=='r' else .30,-.1,1.0))
bpy.context.view_layer.update()
for side in ['r','l']:
 H=arm.matrix_world@arm.pose.bones['hand_'+side].matrix
 C=H@Matrix(anatomy[side]['contact_frame_hand_local'])
 fit[side]['measured_contact_error_m']=(C.translation-contacts[side].translation).length
 fit[side]['contact_world']=list(C.translation)
# Keep visible editable target empties, without claiming surface collision validation.
for side in ['r','l']:
 o=bpy.data.objects.new('FIT_STUDY_Contact_'+side,None);scene.collection.objects.link(o);o.matrix_world=contacts[side];o.empty_display_type='ARROWS';o.empty_display_size=.06
 o['scope']='Position/orientation guide only, not collision certification'
# Materials and a wireframe under-magazine clearance guide.
def mat(name,color,emission=0):
 m=bpy.data.materials.new(name);m.diffuse_color=(*color,1);m.use_nodes=True;b=m.node_tree.nodes.get('Principled BSDF');b.inputs['Base Color'].default_value=(*color,1);b.inputs['Roughness'].default_value=.7
 if emission:b.inputs['Emission Color'].default_value=(*color,1);b.inputs['Emission Strength'].default_value=emission
 return m
cyan=mat('FIT_STUDY clearance guide',(0.08,.65,.67),.4);floor_mat=mat('FIT_STUDY floor',(.035,.048,.059));label_mat=mat('FIT_STUDY label',(.65,.76,.78),.2)
def rod(a,b,r,material,name):
 a=Vector(a);b=Vector(b);bpy.ops.mesh.primitive_cylinder_add(vertices=10,radius=r,depth=(b-a).length,location=(a+b)/2);o=bpy.context.object;o.name=name;o.rotation_euler=(b-a).to_track_quat('Z','Y').to_euler();o.data.materials.append(material);return o
# 230mm visual travel envelope below original magazine: illustrative space reservation only.
boxpts=[W@Vector((x,y,z)) for z in [-.205,-.435] for y in [-.043,.043] for x in [-.025,.12]]
for i,j in [(0,1),(0,2),(1,3),(2,3),(4,5),(4,6),(5,7),(6,7),(0,4),(1,5),(2,6),(3,7)]:rod(boxpts[i],boxpts[j],.0018,cyan,'FIT_STUDY_Magazine_ClearanceGuide')
# Studio.
bpy.ops.mesh.primitive_plane_add(size=200,location=(0,0,-.008));bpy.context.object.name='FIT_STUDY_Floor';bpy.context.object.data.materials.append(floor_mat)
for name,loc,power,size in [('Key',(-2,-3,4),850,3),('Fill',(3,-2,2.6),500,3),('Rim',(1,2,3),1000,2)]:
 d=bpy.data.lights.new('FIT_STUDY_'+name,'AREA');d.energy=power;d.shape='DISK';d.size=size;o=bpy.data.objects.new(d.name,d);scene.collection.objects.link(o);o.location=loc;o.rotation_euler=(Vector((0,0,1))-o.location).to_track_quat('-Z','Y').to_euler()
bpy.ops.object.camera_add(location=(-2.7,-5.5,2.25));camera=bpy.context.object;camera.name='FIT_STUDY_CAMERA';target=Vector((.05,-.12,.91));camera.rotation_euler=(target-camera.location).to_track_quat('-Z','Y').to_euler();camera.data.type='ORTHO';camera.data.ortho_scale=2.10;scene.camera=camera
# Camera-aligned labels above and below the model.
def text_label(body,local_xy,size):
 cu=bpy.data.curves.new('FIT_STUDY_LABEL','FONT');cu.body=body;cu.size=size;cu.align_x='CENTER';o=bpy.data.objects.new('FIT_STUDY_LABEL_'+body,cu);scene.collection.objects.link(o);o.rotation_euler=camera.rotation_euler;right=camera.rotation_euler.to_matrix()@Vector((1,0,0));up=camera.rotation_euler.to_matrix()@Vector((0,1,0));o.location=target+right*local_xy[0]+up*local_xy[1];o.data.materials.append(label_mat)
text_label('KITE-01 / CHR01', (0,.98), .057)
text_label('STATIC FIT STUDY  /  NOT ANIMATION OR COLLISION ACCEPTANCE', (0,-.94), .025)
text_label('Cyan: illustrative magazine movement space', (0,-.995), .026)
scene.world.color=(.09,.09,.09);scene.render.engine='CYCLES';scene.cycles.samples=24;scene.cycles.use_denoising=False
scene.render.resolution_x=1400;scene.render.resolution_y=1600;scene.render.resolution_percentage=100;scene.view_settings.view_transform='AgX';scene.render.image_settings.file_format='PNG'
scene['artifact_identity']='KITE01_CHR01_FIT_STUDY';scene['scope']='Static Blender space review; target closure only; no complete collision, trigger finger, aim, reload animation or UE acceptance'
scene.frame_start=1;scene.frame_end=1;scene.frame_set(1)
# Pack existing exact image payloads in the separate derivative for portable review.
bpy.ops.file.pack_all()
rest_after=digest(rest_record());mesh_after={o.name:mesh_record(o) for o in character_meshes}
assert rest_after==rest_before;assert mesh_after==mesh_before
assert all(sha(ROOT.parent/p)==h for p,h in inputs.items())
report={'status':'STATIC_FIT_STUDY_PENDING_VISUAL_REVIEW','character_source_is_derivative':True,'original_frozen_character_file_obtained':False,'inputs_sha256_before':inputs,'inputs_sha256_after':{p:sha(ROOT.parent/p) for p in inputs},'rig':arm.name,'bone_count':len(arm.data.bones),'rest_sha256_before':rest_before,'rest_sha256_after':rest_after,'rest_preserved':rest_before==rest_after,'character_mesh_vertices_topology_weights_preserved':mesh_before==mesh_after,'character_mesh_fingerprints':mesh_before,'fit':fit,'rifle_world_matrix':[list(r) for r in W],'pose':'Across-body low-ready static fit study','contact_frames_rifle_local':{k:[list(r) for r in m] for k,m in {'r':Cr,'l':Cl}.items()},'contact_frames_world':{k:[list(r) for r in m] for k,m in contacts.items()},'suggested_socket_rotation_quaternion_xyzw':{k:[m.to_quaternion().x,m.to_quaternion().y,m.to_quaternion().z,m.to_quaternion().w] for k,m in {'Grip_R':Cr,'Support_L':Cl}.items()},'socket_suggestion_scope':'Art-fit candidates only, no Grip32 collision compatibility claim; support translation lower by 24mm','right_fingers':'Grip32 source quaternion pose','left_fingers':'55 percent source Grip32 curl for approximate under-pad support','support_contact_offset_from_socket_local_m':[0,0,-.024],'magazine_guide_travel_m':.23,'checks':['source SHA unchanged','all 65 bone REST records unchanged','character mesh vertices/topology/weights unchanged','analytical two-bone reach','evaluated hand contact-frame positional closure'],'not_checked':['continuous hand-versus-gun surface collision','finger surface fit to rectangular support pad and angled grip','trigger finger placement','clothing-versus-gun collision','full-body animation','reload hand animation','aiming/sight alignment','Unreal import/runtime'],'output_blend':'KITE01_CharacterFit.blend','output_render':'renders/KITE01_CharacterFit.png'}
(ROOT/'docs/Character_Fit_Study.json').write_text(json.dumps(report,ensure_ascii=False,indent=2))
bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'KITE01_CharacterFit.blend'))
scene.render.filepath=str(ROOT/'renders/KITE01_CharacterFit.png');bpy.ops.render.render(write_still=True)
print('FIT_REPORT',json.dumps(fit))
