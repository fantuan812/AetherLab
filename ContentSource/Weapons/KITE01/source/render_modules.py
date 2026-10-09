import bpy,math
from pathlib import Path
from mathutils import Vector
R=Path(__file__).resolve().parents[1]
bpy.ops.wm.open_mainfile(filepath=str(R/'KITE01_Modular.blend'))
s=bpy.context.scene;s.cycles.use_denoising=False;s.cycles.samples=48
for c in bpy.data.collections:c.hide_viewport=False
for o in bpy.data.objects:
 if o.type=='MESH' and o.name.startswith('SM_'):o.hide_render=True
labelmat=bpy.data.materials.new('Review_Label');labelmat.use_nodes=True;bs=labelmat.node_tree.nodes.get('Principled BSDF');bs.inputs['Base Color'].default_value=(.75,.85,.85,1);bs.inputs['Emission Color'].default_value=(.55,.65,.65,1);bs.inputs['Emission Strength'].default_value=.5
names=['Receiver','Handguard_Sand','Stock_Skeleton','Magazine_Box','Grip_Angled','Optic_Reflex','Muzzle_Short','ChargingHandle','Trigger','Stock_Compact','Muzzle_Cover']
for i,n in enumerate(names):
 o=bpy.data.objects['SM_KITE01_'+n];o.hide_render=False;o.animation_data_clear();o.parent=None
 o.location=(0,0,0);bpy.context.view_layer.update()
 # laid out as separate visual modules, not mechanical assembly instructions
 pts=[o.matrix_world@Vector(v) for v in o.bound_box];cx=(max(p.x for p in pts)+min(p.x for p in pts))/2;cz=(max(p.z for p in pts)+min(p.z for p in pts))/2
 col=i%3;row=i//3;o.location=Vector(((col-1)*.43-cx,0,.63-row*.29-cz))
 curve=bpy.data.curves.new(n,'FONT');curve.body=n.replace('_',' ');curve.size=.019;curve.align_x='CENTER';curve.extrude=0
 tx=bpy.data.objects.new('Label_'+n,curve);s.collection.objects.link(tx);tx.location=((col-1)*.43,-.065,.49-row*.29);tx.rotation_euler=(math.pi/2,0,0)
 curve.materials.append(labelmat)
s.camera.location=(0,-3,.16);s.camera.rotation_euler=(math.pi/2,0,0);s.camera.data.ortho_scale=1.5
bpy.data.objects['STUDIO_Floor'].hide_render=True
s.render.resolution_x=1500;s.render.resolution_y=1500;s.world.color=(.30,.30,.30)
s.render.film_transparent=False;s.render.filepath=str(R/'renders/KITE01_Modules.png');bpy.ops.render.render(write_still=True)
