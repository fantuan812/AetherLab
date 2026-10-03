"""Blender: --background --python this.py -- INPUT.blend OUTPUT.blend
Only use on the pre-repair CHR01 Hammer demo. Never apply to character master.
"""
import bpy,sys
from mathutils import Vector
args=sys.argv[sys.argv.index('--')+1:];bpy.ops.wm.open_mainfile(filepath=args[0])
for name in ['CHR01 • ivory split shoulder cape','CHR01 • ivory split shoulder cape_low']:
 o=bpy.data.objects[name]
 if 'CapeInnerShell_Hammer_v2' in o.data.shape_keys.key_blocks:raise RuntimeError('Already repaired')
 k=o.shape_key_add(name='CapeInnerShell_Hammer_v2',from_mix=False)
 for v,p in zip(o.data.shape_keys.key_blocks[0].data,k.data):
  x,y,z=v.co;rr=((x+.164)/.07)**2+((y-.038)/.085)**2+((z-1.345)/.085)**2;w=max(0,1-rr)**2
  p.co+=Vector((0,.004*w,.008*w))
 old=next(f.driver for f in o.data.shape_keys.animation_data.drivers if 'CapeClearance_Hammer' in f.data_path)
 d=k.driver_add('value').driver;d.expression=old.expression
 for v in old.variables:
  nv=d.variables.new();nv.name=v.name;nv.type=v.type
  for a,b in zip(nv.targets,v.targets):
   a.id=b.id;a.data_path=b.data_path;a.bone_target=b.bone_target;a.transform_type=b.transform_type;a.transform_space=b.transform_space;a.rotation_mode=b.rotation_mode;a.use_fallback_value=b.use_fallback_value;a.fallback_value=b.fallback_value
bpy.context.scene.frame_set(73);bpy.context.view_layer.update();bpy.ops.wm.save_as_mainfile(filepath=args[1],compress=True)
