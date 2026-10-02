"""Render a handful of authored poses. No UE compile, rules or startup tests."""
import bpy
from pathlib import Path
from mathutils import Vector

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'Art'/'WindwardHero'
bpy.ops.wm.open_mainfile(filepath=str(OUT/'WindwardHero.blend'))
scene=bpy.context.scene
rig=bpy.data.objects['Windward_Rig']
studio=bpy.data.collections['STUDIO | excluded from exports']
studio.hide_viewport=False
scene.cycles.samples=32
scene.render.resolution_x=900
scene.render.resolution_y=900
cam=scene.camera
cam.location=(2.5,-6,2.75)
cam.rotation_euler=(Vector((0,0,.99))-cam.location).to_track_quat('-Z','Y').to_euler()

for name,action,frame in [('Pose_Walk','Walk_InPlace',5),('Pose_Wave','Wave_Hello',37),('Pose_Slash','Sword_Slash',19)]:
    rig.animation_data.action=bpy.data.actions[action]
    scene.frame_set(frame)
    bpy.data.objects['Windward_Sword'].hide_render=(action=='Wave_Hello')
    scene.render.filepath=str(OUT/'Previews'/f'{name}.png')
    bpy.ops.render.render(write_still=True)
    print('AUTHORED_POSE',name,action,frame,flush=True)

# Bone overlay from the real armature, on the real relaxed skinned model.
rig.animation_data.action=bpy.data.actions['Idle_Breathe'];scene.frame_set(1)
bpy.data.objects['Windward_Sword'].hide_render=True
bpy.data.objects['Windward_Shield'].hide_render=True
for ob in [bpy.data.objects['Windward_Body'],bpy.data.objects['Windward_Head'],bpy.data.objects['Windward_Hair']]:
    for mat in ob.data.materials:
        if mat and mat.use_nodes:
            p=next((n for n in mat.node_tree.nodes if n.type=='BSDF_PRINCIPLED'),None)
            if p is None:continue
            for link in list(p.inputs['Base Color'].links):mat.node_tree.links.remove(link)
            p.inputs['Base Color'].default_value=(.15,.23,.25,1)
            p.inputs['Roughness'].default_value=.95
            p.inputs['Subsurface Weight'].default_value=0
gold=bpy.data.materials.new('Rig preview emission')
gold.use_nodes=True
p=gold.node_tree.nodes.get('Principled BSDF')
p.inputs['Base Color'].default_value=(1,.57,.1,1)
p.inputs['Emission Color'].default_value=(1,.3,.04,1)
p.inputs['Emission Strength'].default_value=.35
depsgraph=bpy.context.evaluated_depsgraph_get()
evaluated=rig.evaluated_get(depsgraph)
for b in evaluated.pose.bones:
    if not b.bone.use_deform:continue
    a=rig.matrix_world@b.head;d=rig.matrix_world@b.tail
    # Shift toward camera only for the illustrative overlay; armature is unchanged.
    offset=Vector((.14,-.40,.10))
    a+=offset;d+=offset
    mid=(a+d)/2
    bpy.ops.mesh.primitive_cone_add(vertices=4,radius1=.009 if '01_' in b.name or '02_' in b.name or '03_' in b.name else .014,
                                   radius2=.002,depth=(d-a).length,location=mid)
    ob=bpy.context.object;ob.rotation_euler=(d-a).to_track_quat('Z','Y').to_euler();ob.data.materials.append(gold)
scene.render.filepath=str(OUT/'Previews'/'Rig_Overlay.png')
bpy.ops.render.render(write_still=True)
print('Rig overlay illustration rendered; source .blend remains unchanged',flush=True)
