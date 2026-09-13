import bpy,json
from pathlib import Path
OUT=Path('C:/ueproject/test/Art/SwordMagic')
def export(name,obs):
    bpy.ops.object.select_all(action='DESELECT')
    for ob in obs:ob.select_set(True)
    bpy.context.view_layer.objects.active=obs[0]
    bpy.ops.export_scene.fbx(filepath=str(OUT/'Exports/FBX'/f'{name}.fbx'),use_selection=True,object_types={'MESH','ARMATURE'},apply_unit_scale=True,apply_scale_options='FBX_SCALE_UNITS',axis_forward='-Y',axis_up='Z',add_leaf_bones=False,bake_anim=False,use_custom_props=True)
    bpy.ops.export_scene.gltf(filepath=str(OUT/'Exports/GLB'/f'{name}.glb'),use_selection=True,export_format='GLB',export_animations=False)
bpy.context.window.scene=bpy.data.scenes['SM_02_Characters']
for name,factor in [('Oathwanderer',.925),('BellKnight_Auren',1.62)]:
    body=bpy.data.objects['SK_'+name];rig=bpy.data.objects['Rig_'+name]
    if not body.get('TorsoClearanceFixed'):
        spine=body.vertex_groups['spine'].index
        ids={i:m.name for i,m in enumerate(body.data.materials)}
        vertex_material={vi:ids[p.material_index] for p in body.data.polygons for vi in p.vertices}
        for v in body.data.vertices:
            if not any(g.group==spine for g in v.groups):continue
            material=vertex_material[v.index]
            if material=='M_SM_Leather':v.co.y*=.55
            elif material in ('M_SM_Teal','M_SM_Crimson','M_SM_Gold','M_SM_Amber') and v.co.y<0:v.co.y-=.045*factor
        body['TorsoClearanceFixed']=True
    original=rig.location.copy();rig.location=(0,0,0);bpy.context.view_layer.update()
    export('SK_'+name,[body,rig]);rig.location=original;bpy.context.view_layer.update()
sc=bpy.data.scenes['SM_01_BrokenBellAbbey'];bpy.context.window.scene=sc
if not bpy.data.objects.get('LV_BellSuspension'):
    bpy.ops.mesh.primitive_cylinder_add(vertices=12,radius=.1,depth=1.15,location=(0,34,11.825))
    ob=bpy.context.object;ob.name='LV_BellSuspension';ob.data.materials.append(bpy.data.materials['M_SM_Steel'])
    ob['Prototype']='Broken Bell Abbey';ob['Units']='metres'
meshes=[o for o in sc.objects if o.type=='MESH' and not o.name.startswith('Preview_')]
export('SM_BrokenBellAbbey_Layout',meshes)
manifest=json.loads((OUT/'asset-manifest.json').read_text(encoding='utf-8'))
rows=[]
for ob in meshes:
    ob.data.calc_loop_triangles();rows.append(dict(name=ob.name,triangles=len(ob.data.loop_triangles),dimensions_m=list(ob.dimensions),uv_layers=len(ob.data.uv_layers)))
manifest['level'].update(meshes=len(rows),triangles=sum(r['triangles'] for r in rows),objects=rows)
(OUT/'asset-manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'BrokenBellAbbey_Characters.blend'))
print('Refined torso clearance and bell suspension; source and all exchange files updated.')
