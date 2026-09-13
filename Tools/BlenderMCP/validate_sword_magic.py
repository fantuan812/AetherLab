"""Independent checks on the saved Blender source and its exchange files."""
import bpy,bmesh,json,math
from pathlib import Path
OUT=Path('C:/ueproject/test/Art/SwordMagic')
bpy.ops.wm.open_mainfile(filepath=str(OUT/'BrokenBellAbbey_Characters.blend'))
report={'errors':[],'characters':[],'level_mesh_count':0,'exports':[]}
sc=bpy.data.scenes['SM_01_BrokenBellAbbey']
for ob in sc.objects:
    if ob.type!='MESH':continue
    report['level_mesh_count']+=1
    if any(not math.isfinite(x) for v in ob.data.vertices for x in v.co):report['errors'].append(ob.name+': invalid coordinate')
    if not ob.data.uv_layers:report['errors'].append(ob.name+': missing UV')
    bm=bmesh.new();bm.from_mesh(ob.data)
    nonmanifold=sum(not e.is_manifold for e in bm.edges);bm.free()
    if nonmanifold:report['errors'].append(ob.name+': open/nonmanifold mesh')
for name in ('Oathwanderer','BellKnight_Auren'):
    ob=bpy.data.objects['SK_'+name];rig=bpy.data.objects['Rig_'+name]
    unweighted=sum(not v.groups or abs(sum(g.weight for g in v.groups)-1)>1e-4 for v in ob.data.vertices)
    if unweighted:report['errors'].append(name+': unweighted/unnormalized vertices')
    unknown=[g.name for g in ob.vertex_groups if g.name not in rig.data.bones]
    if unknown:report['errors'].append(name+': unknown bone groups')
    # Exercise an actual bone and check evaluated geometry moves, then restore.
    bpy.context.window.scene=bpy.data.scenes['SM_02_Characters']
    dg=bpy.context.evaluated_depsgraph_get();before=[v.co.copy() for v in ob.evaluated_get(dg).data.vertices]
    bone=rig.pose.bones['lowerarm_l'];bone.rotation_mode='XYZ';bone.rotation_euler.x=.35;bpy.context.view_layer.update()
    after=[v.co.copy() for v in ob.evaluated_get(dg).data.vertices]
    moved=sum((a-b).length>1e-5 for a,b in zip(after,before));bone.rotation_euler.x=0;bpy.context.view_layer.update()
    if not moved:report['errors'].append(name+': armature did not deform')
    report['characters'].append({'name':name,'bones':len(rig.data.bones),'vertices':len(ob.data.vertices),'unweighted_vertices':unweighted,'pose_test_moved_vertices':moved})
for name in ('SM_BrokenBellAbbey_Layout','SK_Oathwanderer','SK_BellKnight_Auren'):
    for fmt in ('FBX','GLB'):
        p=OUT/'Exports'/fmt/(name+'.'+fmt.lower());size=p.stat().st_size if p.exists() else 0
        report['exports'].append({'file':str(p.relative_to(OUT)),'bytes':size})
        if size<1000:report['errors'].append(str(p)+': missing/empty')
report['passed']=not report['errors']
(OUT/'validation.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print('SWORD_MAGIC_VALIDATION',json.dumps(report))
if report['errors']:raise RuntimeError('Prototype validation failed')
