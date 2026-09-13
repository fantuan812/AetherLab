import bpy,json,math
from pathlib import Path
from mathutils import Vector
OUT=Path('C:/ueproject/test/Art/SwordMagic')
report={'files':[],'errors':[]}
for name,expected in [('SK_Oathwanderer',1.8685),('SK_BellKnight_Auren',3.3129),('SM_BrokenBellAbbey_Layout',None)]:
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=str(OUT/'Exports/FBX'/f'{name}.fbx'))
    obs=[o for o in bpy.context.scene.objects if o.type=='MESH']
    rigs=[o for o in bpy.context.scene.objects if o.type=='ARMATURE']
    points=[ob.matrix_world@Vector(c) for ob in obs for c in ob.bound_box]
    dims=[max(v[i] for v in points)-min(v[i] for v in points) for i in range(3)]
    row={'file':name+'.fbx','meshes':len(obs),'armatures':len(rigs),'dimensions_m':dims}
    if expected is not None:
        row['expected_height_m']=expected
        if len(rigs)!=1 or len(rigs[0].data.bones)!=17:report['errors'].append(name+': unexpected imported rig')
        if abs(dims[2]-expected)>.01:report['errors'].append(name+': incorrect units/height')
    elif len(obs)!=457:report['errors'].append('Layout mesh count changed during FBX round trip')
    report['files'].append(row)
report['passed']=not report['errors']
(OUT/'fbx-roundtrip-validation.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print('FBX_ROUNDTRIP',json.dumps(report))
if report['errors']:raise RuntimeError('FBX round trip failed')
