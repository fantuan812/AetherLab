import bpy,json
from pathlib import Path
from mathutils import Vector,Matrix
root=Path('C:/ueproject/test/Art/SwordMagic/Modular')
report=json.loads((root/'manifest.json').read_text())
scene=bpy.data.scenes['SM_01_BrokenBellAbbey'];bpy.context.window.scene=scene
for ob in list(scene.objects):
    if ob.type!='MESH' or not ob.name.startswith('LV_Apprentice'):continue
    # Move the entire NPC/bench group out of the stone arcade pier.
    ob.location.x=13.4;bpy.context.view_layer.update()
    center=sum((ob.matrix_world@Vector(c) for c in ob.bound_box),Vector())/8
    cp=ob.copy();cp.data=ob.data.copy();scene.collection.objects.link(cp);cp.parent=None
    for v in cp.data.vertices:v.co=ob.matrix_world@v.co-center
    cp.matrix_world=Matrix.Identity(4)
    name='SM_Interact_'+ob.name[3:].replace('.','_');cp.name='UE_FIX_'+name
    bpy.ops.object.select_all(action='DESELECT');cp.hide_set(False);cp.select_set(True);bpy.context.view_layer.objects.active=cp
    bpy.ops.export_scene.fbx(filepath=str(root/'FBX'/f'{name}.fbx'),use_selection=True,object_types={'MESH'},apply_unit_scale=True,apply_scale_options='FBX_SCALE_UNITS',axis_forward='-Y',axis_up='Z',add_leaf_bones=False,bake_anim=False,use_custom_props=True)
    for entry in report['level']:
        if entry['name']==name:entry['location_m']=list(center)
    bpy.data.objects.remove(cp,do_unlink=True)
(root/'manifest.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
bpy.ops.wm.save_as_mainfile(filepath=str(root/'BrokenBellAbbey_Modular.blend'))
print('NPC_PLACEMENT_FIXED')
