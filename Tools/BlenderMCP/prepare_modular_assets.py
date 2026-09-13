"""Executed through Blender MCP. Preserve the original source; export UE-ready pieces."""
import bpy,bmesh,json,math,traceback
from pathlib import Path
from mathutils import Vector
ROOT=Path('C:/ueproject/test');OUT=ROOT/'Art/SwordMagic/Modular'
for p in (OUT,OUT/'FBX'):p.mkdir(parents=True,exist_ok=True)
report={'characters':[],'equipment':[],'level':[],'materials':{},'errors':[]}
def select(obs):
    bpy.ops.object.select_all(action='DESELECT')
    for ob in obs:ob.hide_set(False);ob.select_set(True)
    bpy.context.view_layer.objects.active=obs[0]
def export(name,obs,anim=False):
    select(obs);bpy.ops.export_scene.fbx(filepath=str(OUT/'FBX'/f'{name}.fbx'),use_selection=True,object_types={'MESH','ARMATURE'},apply_unit_scale=True,apply_scale_options='FBX_SCALE_UNITS',axis_forward='-Y',axis_up='Z',add_leaf_bones=False,bake_anim=anim,bake_anim_use_all_actions=False,bake_anim_use_nla_strips=False,use_custom_props=True)
def parts(ob):
    adjacent=[[] for v in ob.data.vertices]
    for e in ob.data.edges:a,b=e.vertices;adjacent[a].append(b);adjacent[b].append(a)
    visited=set();out=[]
    for v in ob.data.vertices:
        if v.index in visited:continue
        todo=[v.index];ids=[];visited.add(v.index)
        while todo:
            a=todo.pop();ids.append(a)
            for b in adjacent[a]:
                if b not in visited:visited.add(b);todo.append(b)
        out.append(ids)
    return out
def keep_vertices(ob,ids):
    bm=bmesh.new();bm.from_mesh(ob.data);bm.verts.ensure_lookup_table()
    bmesh.ops.delete(bm,geom=[v for v in bm.verts if v.index not in ids],context='VERTS')
    bm.to_mesh(ob.data);bm.free();ob.data.update()
def info(ob):
    ob.data.calc_loop_triangles();bpy.context.view_layer.update()
    return {'name':ob.name,'triangles':len(ob.data.loop_triangles),'materials':[m.name for m in ob.data.materials],'dimensions_m':list(ob.dimensions)}
def copy_mesh(src,name,sc):
    ob=src.copy();ob.data=src.data.copy();ob.name=name;sc.collection.objects.link(ob);return ob
def build():
    scene=bpy.data.scenes.new('SM_03_ModularCharacters');bpy.context.window.scene=scene
    scene.unit_settings.system='METRIC';scene.unit_settings.scale_length=1
    for name,factor in [('Oathwanderer',.925),('BellKnight_Auren',1.62)]:
        source=bpy.data.objects['SK_'+name];srcrig=bpy.data.objects['Rig_'+name]
        rig=srcrig.copy();rig.data=srcrig.data.copy();rig.name='RIG_Modular_'+name;scene.collection.objects.link(rig);rig.location=(0,0,0)
        body=copy_mesh(source,'SK_Modular_'+name,scene);body.parent=rig;body.matrix_parent_inverse.identity();body.location=(0,0,0)
        for mod in body.modifiers:
            if mod.type=='ARMATURE':mod.object=rig
        groups=parts(body);remove=set()
        for hand,item in [('hand_r','SM_OathSword' if name=='Oathwanderer' else 'SM_BellHammer'),('hand_l','SM_OathShield')]:
            if name!='Oathwanderer' and hand=='hand_l':continue
            ids=[];gauntlets=0
            center=Vector(((.53 if hand=='hand_l' else -.53)*factor,-.09*factor,.90*factor))
            for group in groups:
                v=body.data.vertices[group[0]]
                if not any(body.vertex_groups[g.group].name==hand for g in v.groups):continue
                c=sum((body.data.vertices[i].co for i in group),Vector())/len(group)
                if len(group)==42 and (c-center).length<.002:gauntlets+=1;continue
                ids.extend(group)
            assert gauntlets==1 and ids,(name,hand,gauntlets)
            prop=copy_mesh(body,item,scene);prop.parent=None
            for mod in list(prop.modifiers):prop.modifiers.remove(mod)
            keep_vertices(prop,set(ids));remove.update(ids);prop.vertex_groups.clear()
            # Bone-local geometry: the hand joint is the independent equipment pivot.
            inv=rig.data.bones[hand].matrix_local.inverted()
            for v in prop.data.vertices:v.co=inv@v.co
            prop.location=(0,0,0);prop.rotation_euler=(0,0,0);prop.scale=(1,1,1)
            export(item,[prop]);record=info(prop);record.update(socket=hand,source_character=name,pivot='bone_local',parts=len([g for g in groups if g[0] in ids]))
            report['equipment'].append(record)
            prop.matrix_world=rig.data.bones[hand].matrix_local;prop.hide_set(True);prop.hide_render=True
        keep_vertices(body,set(range(len(body.data.vertices)))-remove)
        body['EquipmentGeometryRemoved']=True;export(body.name,[body,rig])
        record=info(body);record.update(bones=len(rig.data.bones),removed_vertices=len(remove));report['characters'].append(record)
        # A small editable locomotion clip; combat authority does not depend on this animation.
        rig.animation_data_create()
        for action_kind in ('Walk','Attack'):
            action=bpy.data.actions.new('AN_'+name+'_'+action_kind);rig.animation_data.action=action
            scene.frame_start=1;scene.frame_end=25;scene.render.fps=30
            for f in range(1,26,3):
                t=(f-1)/24
                for bone in rig.pose.bones:bone.rotation_mode='XYZ';bone.rotation_euler=(0,0,0)
                if action_kind=='Walk':
                    for side,s in [('l',1),('r',-1)]:
                        rig.pose.bones['thigh_'+side].rotation_euler.x=.34*math.sin(t*math.tau)*s
                        rig.pose.bones['calf_'+side].rotation_euler.x=max(0,-.4*math.sin(t*math.tau)*s)
                        rig.pose.bones['upperarm_'+side].rotation_euler.x=-.18*math.sin(t*math.tau)*s
                else:
                    a=math.sin(t*math.pi);rig.pose.bones['upperarm_r'].rotation_euler.x=-.9*a;rig.pose.bones['spine'].rotation_euler.y=.18*a
                for bone in rig.pose.bones:bone.keyframe_insert('rotation_euler',frame=f,group=bone.name)
            scene.frame_set(1);export(action.name,[body,rig],True)
            rig.animation_data.action=None
        for bone in rig.pose.bones:bone.rotation_euler=(0,0,0)
        rig.location=(-1.5 if name=='Oathwanderer' else 1.5,0,0)
    # Export the art layout as static collision shells and independent interaction meshes.
    level=bpy.data.scenes['SM_01_BrokenBellAbbey'];bpy.context.window.scene=level
    for ob in level.objects:
        if ob.type=='MESH' and ob.name.startswith('LV_Apprentice'):ob.location.x=13.4
    bpy.context.view_layer.update()
    temp=bpy.data.collections.new('UE_ExportCopies');level.collection.children.link(temp)
    keep=[o for o in level.objects if o.type=='MESH' and not o.name.startswith('Preview_')]
    interactive_prefixes=('LV_BridgePlank','LV_CuttableRope','LV_CoverCrate','LV_ConductivePuddle','LV_CisternWater','LV_AncientSigil','LV_FrostCrossing','LV_Apprentice','LV_OathRecord','LV_WitnessBell','LV_CanalWater')
    classified={};static=[]
    for src in keep:
        if src.name.startswith(interactive_prefixes):classified[src.name]=src
        else:static.append(src)
    def bake(srcs,name,center):
        copies=[]
        for src in srcs:
            ob=copy_mesh(src,'UE_TMP',level);ob.parent=None
            for v in ob.data.vertices:v.co=src.matrix_world@v.co-Vector(center)
            ob.location=(0,0,0);ob.rotation_euler=(0,0,0);ob.scale=(1,1,1);copies.append(ob)
        select(copies);bpy.ops.object.join();ob=bpy.context.object;ob.name=name
        export(name,[ob]);record=info(ob);record.update(location_m=list(center));report['level'].append(record)
        bpy.data.objects.remove(ob,do_unlink=True)
    bake(static,'SM_Abbey_StaticShell',(0,0,0))
    for name,src in classified.items():
        center=sum((src.matrix_world@Vector(c) for c in src.bound_box),Vector())/8
        bake([src],'SM_Interact_'+name[3:].replace('.','_'),center)
    for m in bpy.data.materials:
        if m.name.startswith('M_SM_'):
            bs=m.node_tree.nodes.get('Principled BSDF')
            if bs:report['materials'][m.name]={'color':list(bs.inputs['Base Color'].default_value)[:3],'metallic':bs.inputs['Metallic'].default_value,'roughness':bs.inputs['Roughness'].default_value,'emission':bs.inputs['Emission Strength'].default_value}
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'BrokenBellAbbey_Modular.blend'))
    report['passed']=True;(OUT/'manifest.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
try:build()
except Exception:
    report['errors'].append(traceback.format_exc());report['passed']=False;(OUT/'manifest.json').write_text(json.dumps(report,indent=2));raise
print('MODULAR_ASSETS_COMPLETE',json.dumps({'characters':report['characters'],'equipment':report['equipment'],'level_assets':len(report['level'])}))
