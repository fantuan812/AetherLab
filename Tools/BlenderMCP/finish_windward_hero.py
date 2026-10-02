"""Finish the earlier blockout in the user's explicit nine-stage order.

The existing rig is dismantled first. The final rig/skin/actions are created only
after topology, normals, UVs, actual high-to-low bakes, materials, skin and hair.
No UE compile, gameplay rule test or startup test is run by this script.
"""
import bpy
import bmesh
import math
import json
import importlib.util
import sys
import numpy as np
from pathlib import Path
from mathutils import Vector, noise

sys.dont_write_bytecode=True

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'Art'/'WindwardHero'
TEX=OUT/'Textures';TEX.mkdir(exist_ok=True)
UV=OUT/'UV';UV.mkdir(exist_ok=True)
REPORT={'workflow':['轮廓拓扑','法线','UV','高模烘焙','材质','皮肤 Shader','毛发','骨骼权重','动画'],
        'stages':[], 'scope':'Blender art experiment. UE 未编译、未测试。'}
LOW=[];HIGH={};MAPS={};SEEDS={};SPECS=[];PALETTE={};RIG=None


def record(stage, **facts):
    REPORT['stages'].append({'stage':stage,**facts})
    (OUT/'workflow_report.json').write_text(json.dumps(REPORT,ensure_ascii=False,indent=2),encoding='utf-8')
    print('WORKFLOW',stage,json.dumps(facts,ensure_ascii=False),flush=True)


def select(obs,active=None):
    bpy.ops.object.select_all(action='DESELECT')
    for ob in obs:ob.hide_set(False);ob.select_set(True)
    bpy.context.view_layer.objects.active=active or obs[-1]


def stage_topology():
    # Re-use the authored silhouette, then remove all previous rigging and actions.
    bpy.ops.wm.open_mainfile(filepath=str(OUT/'WindwardHero.blend'))
    if any(m.get('surface_family') for ob in bpy.context.scene.objects if ob.type=='MESH' for m in ob.data.materials if m):
        raise RuntimeError('Run build_windward_hero.py -- --no-render first. This finisher expects the blockout, not an already finished/manual-edited delivery.')
    old=bpy.data.objects['Windward_Rig']
    old.animation_data_clear()
    for pb in old.pose.bones:
        pb.location=(0,0,0);pb.rotation_quaternion=(1,0,0,0);pb.scale=(1,1,1)
    bpy.context.view_layer.update()
    for b in old.data.bones:
        SPECS.append(dict(name=b.name,head=list(b.head_local),tail=list(b.tail_local),
                          parent=b.parent.name if b.parent else None,deform=b.use_deform,roll_axis=[0,-1,0]))
    LOW.extend(bpy.data.objects[n] for n in ['Windward_Body','Windward_Head','Windward_Hair','Windward_Sword','Windward_Shield'])
    for ob in LOW:
        SEEDS[ob.name]=[{ob.vertex_groups[g.group].name:g.weight for g in v.groups if g.weight>0} for v in ob.data.vertices]
        ob.parent=None
        for m in list(ob.modifiers):
            if m.type=='ARMATURE':ob.modifiers.remove(m)
        ob.vertex_groups.clear()
        # Preserve semantic materials as face IDs, not a final shader.
        PALETTE[ob.name]=[(m.name,tuple(m.diffuse_color),m.node_tree.nodes.get('Principled BSDF').inputs['Metallic'].default_value,
                           m.node_tree.nodes.get('Principled BSDF').inputs['Roughness'].default_value) for m in ob.data.materials]
    ad=old.data;bpy.data.objects.remove(old,do_unlink=True)
    if ad.users==0:bpy.data.armatures.remove(ad)
    for act in list(bpy.data.actions):bpy.data.actions.remove(act)
    for col in list(bpy.data.collections):
        if col.name.startswith('HIGH |'):bpy.data.collections.remove(col)
    record('轮廓拓扑',meshes=len(LOW),vertices=sum(len(o.data.vertices) for o in LOW),
           basis='Authored quad-ring joints and separate head/hair/equipment. Previous rig removed before finishing.',
           limitations='Prototype seams overlap between clothing sections; not a single watertight skin surface.')


def stage_normals():
    for ob in LOW:
        bm=bmesh.new();bm.from_mesh(ob.data)
        bmesh.ops.recalc_face_normals(bm,faces=bm.faces)
        for e in bm.edges:
            if len(e.link_faces)==2:e.smooth=e.calc_face_angle()<math.radians(60)
        bm.to_mesh(ob.data);bm.free();ob.data.update()
        if ob.name in ['Windward_Sword','Windward_Shield']:
            select([ob])
            m=ob.modifiers.new('Crafted hard-surface corner normals','WEIGHTED_NORMAL')
            m.keep_sharp=True;m.weight=45;m.mode='FACE_AREA_WITH_ANGLE'
            bpy.ops.object.modifier_apply(modifier=m.name)
    record('法线',outward_faces='Recalculated with BMesh',sharp_angle_degrees=60,
           hard_surface='Weighted corner normals applied on sword and shield')


def stage_uv():
    for ob in LOW:
        select([ob]);bpy.ops.object.mode_set(mode='EDIT');bpy.ops.mesh.select_all(action='SELECT')
        bpy.ops.uv.smart_project(angle_limit=math.radians(66),island_margin=.014)
        bpy.ops.object.mode_set(mode='OBJECT');ob.data.uv_layers.active.name='UV0_Windward'
        # Genuine mesh UV layout, saved as an editable vector reference.
        uv=ob.data.uv_layers.active.data
        paths=[]
        for poly in ob.data.polygons:
            coords=[uv[i].uv for i in poly.loop_indices]
            pts=' '.join(f'{p.x*1024:.2f},{(1-p.y)*1024:.2f}' for p in coords)
            paths.append(f'<polygon points="{pts}" fill="none" stroke="#5cbbad" stroke-width=".55"/>')
        (UV/f'{ob.name}_UV.svg').write_text('<svg xmlns="http://www.w3.org/2000/svg" width="1024" height="1024" viewBox="0 0 1024 1024"><rect width="1024" height="1024" fill="#142226"/>'+''.join(paths)+'</svg>',encoding='utf-8')
    record('UV',layout='One individually packed UV atlas per exported mesh',padding_fraction=.014,
           outputs=[f'UV/{ob.name}_UV.svg' for ob in LOW],limitations='Automatic islands; manual seam polish remains a production follow-up.')


def new_image(name,size,color_space):
    im=bpy.data.images.new(name,width=size,height=size,alpha=False)
    im.colorspace_settings.name=color_space
    im.file_format='PNG';im.filepath_raw=str(TEX/f'{name}.png')
    return im


def target_image(ob,im):
    for mat in ob.data.materials:
        mat.use_nodes=True
        nt=mat.node_tree
        node=nt.nodes.get('BAKE_TARGET') or nt.nodes.new('ShaderNodeTexImage')
        node.name='BAKE_TARGET';node.image=im;nt.nodes.active=node


def save_image(im):
    im.save()
    arr=np.empty(len(im.pixels),dtype=np.float32);im.pixels.foreach_get(arr)
    sample=arr.reshape((-1,4))[:,:3]
    return {'file':f'Textures/{Path(im.filepath_raw).name}','size':list(im.size),
            'rgb_min':float(sample.min()),'rgb_max':float(sample.max()),
            'rgb_std':float(sample.std())}


def isolate_bake(pair):
    for ob in bpy.context.scene.objects:
        if ob.type=='MESH':ob.hide_render=ob not in pair
    for ob in pair:ob.hide_render=False
    select(pair,pair[-1])


def stage_bake():
    scene=bpy.context.scene
    scene.render.engine='CYCLES';scene.cycles.samples=16
    scene.render.bake.margin=12;scene.render.bake.margin_type='EXTEND'
    scene.render.bake.normal_space='TANGENT'
    scene.render.bake.normal_r='POS_X';scene.render.bake.normal_g='POS_Y';scene.render.bake.normal_b='POS_Z'
    scene.render.bake.cage_extrusion=.016;scene.render.bake.max_ray_distance=.045
    hc=bpy.data.collections.new('HIGH | editable bake sources');scene.collection.children.link(hc)
    ao=bpy.data.materials.new('HIGH local geometric AO')
    ao.use_nodes=True;nt=ao.node_tree;nt.nodes.clear()
    output=nt.nodes.new('ShaderNodeOutputMaterial');emit=nt.nodes.new('ShaderNodeEmission')
    a=nt.nodes.new('ShaderNodeAmbientOcclusion');a.only_local=True;a.samples=8;a.inputs['Distance'].default_value=.14
    nt.links.new(a.outputs['AO'],emit.inputs['Color']);nt.links.new(emit.outputs[0],output.inputs['Surface'])
    bake_facts=[]
    for low in LOW:
        high=low.copy();high.data=low.data.copy();high.name=low.name.replace('Windward_','HP_');hc.objects.link(high)
        # Unique high materials avoid sharing the active bake image on the source.
        names=[p[0] for p in PALETTE[low.name]]
        select([high]);sub=high.modifiers.new('High resolution geometry','SUBSURF');sub.levels=1 if 'Hair' in low.name else 2
        sub.render_levels=sub.levels;bpy.ops.object.modifier_apply(modifier=sub.name)
        cloth=set();leather=set()
        for poly in high.data.polygons:
            name=names[poly.material_index]
            target=cloth if name in ['ForestCloth','LeafCloth','Linen'] else leather if name in ['Leather','LeatherLight'] else None
            if target is not None:target.update(poly.vertices)
        high.data.update()
        normals=[v.normal.copy() for v in high.data.vertices]
        for v in high.data.vertices:
            p=v.co
            if v.index in cloth:
                seam=math.exp(-((p.z-1.05)/.10)**2)+.4*math.exp(-((p.z-.39)/.06)**2)
                fold=.0016*math.sin(p.z*63+p.x*19)*seam
                detail=.00032*noise.noise(p*85)
            elif v.index in leather:fold=0;detail=.0004*noise.noise(p*70)
            else:fold=0;detail=.00008*noise.noise(p*55)
            v.co+=normals[v.index]*(fold+detail)
        high.data.update();HIGH[low.name]=high
        high.data.materials.clear();high.data.materials.append(ao)
        for poly in high.data.polygons:poly.material_index=0;poly.use_smooth=True
        size=2048 if low.name=='Windward_Body' else 1024 if low.name in ['Windward_Head','Windward_Hair','Windward_Shield'] else 512
        normal=new_image(low.name+'_Normal',size,'Non-Color')
        ambient=new_image(low.name+'_AO',min(size,1024),'Non-Color')
        MAPS[low.name]={'Normal':normal,'AO':ambient,'size':size}
        target_image(low,normal);isolate_bake([high,low])
        bpy.ops.object.bake(type='NORMAL',use_selected_to_active=True)
        normal_stats=save_image(normal)
        target_image(low,ambient)
        bpy.ops.object.bake(type='EMIT',use_selected_to_active=True)
        ao_stats=save_image(ambient)
        bake_facts.append({'low':low.name,'high_vertices':len(high.data.vertices),'normal':normal_stats,'AO':ao_stats})
        print('BAKED',low.name,'high vertices',len(high.data.vertices),flush=True)
    for ob in bpy.context.scene.objects:
        if ob.type=='MESH':ob.hide_render=ob in HIGH.values()
    hc.hide_render=True;hc.hide_viewport=True
    record('高模烘焙',method='Cycles selected-to-active high-to-low; tangent +Y normals and local geometric AO',
           cage_extrusion_m=.016,max_ray_distance_m=.045,bakes=bake_facts,
           source='Subdivision and explicit geometric cloth folds/leather grain; high meshes preserved in .blend',
           limitations='Procedural high-poly detail prototype; no manual sculpt session or production cage review claimed.')


def stage_materials():
    for ob in LOW:
        face_material_ids=[poly.material_index for poly in ob.data.polygons]
        mats=[]
        for name,color,metal,rough in PALETTE[ob.name]:
            m=bpy.data.materials.new(f'WH_{ob.name}_{name}');m.use_nodes=True;m.diffuse_color=color
            m['surface_family']=name
            nt=m.node_tree;nt.nodes.clear()
            out=nt.nodes.new('ShaderNodeOutputMaterial');p=nt.nodes.new('ShaderNodeBsdfPrincipled')
            p.inputs['Base Color'].default_value=color;p.inputs['Metallic'].default_value=metal;p.inputs['Roughness'].default_value=rough
            nt.links.new(p.outputs[0],out.inputs['Surface'])
            texcoord=nt.nodes.new('ShaderNodeTexCoord')
            n=nt.nodes.new('ShaderNodeTexNoise');n.inputs['Scale'].default_value=90 if 'Cloth' in name or name=='Linen' else 65
            n.inputs['Detail'].default_value=2
            nt.links.new(texcoord.outputs['Generated'],n.inputs['Vector'])
            ramp=nt.nodes.new('ShaderNodeValToRGB')
            ramp.color_ramp.elements[0].color=(.87,.87,.87,1);ramp.color_ramp.elements[1].color=(1.07,1.07,1.07,1)
            nt.links.new(n.outputs['Fac'],ramp.inputs[0])
            tint=nt.nodes.new('ShaderNodeMixRGB');tint.blend_type='MULTIPLY';tint.inputs[0].default_value=.46
            tint.inputs[1].default_value=color;nt.links.new(ramp.outputs['Color'],tint.inputs[2])
            occ=nt.nodes.new('ShaderNodeTexImage');occ.name='BAKED_AO';occ.image=MAPS[ob.name]['AO']
            color_mix=nt.nodes.new('ShaderNodeMixRGB');color_mix.blend_type='MULTIPLY';color_mix.inputs[0].default_value=.22
            nt.links.new(tint.outputs[0],color_mix.inputs[1]);nt.links.new(occ.outputs['Color'],color_mix.inputs[2])
            nt.links.new(color_mix.outputs[0],p.inputs['Base Color'])
            if 'Cloth' in name or name=='Linen':p.inputs['Sheen Weight'].default_value=.13
            norm=nt.nodes.new('ShaderNodeTexImage');norm.name='HIGH_BAKED_NORMAL';norm.image=MAPS[ob.name]['Normal']
            nm=nt.nodes.new('ShaderNodeNormalMap');nm.uv_map='UV0_Windward';nm.inputs['Strength'].default_value=.65
            nt.links.new(norm.outputs['Color'],nm.inputs['Color']);nt.links.new(nm.outputs['Normal'],p.inputs['Normal'])
            mats.append(m)
        ob.data.materials.clear()
        for m in mats:ob.data.materials.append(m)
        for poly,index in zip(ob.data.polygons,face_material_ids):poly.material_index=index
        # Bake the final color atlas; lighting is excluded, AO is deliberately subtle.
        base=new_image(ob.name+'_BaseColor',MAPS[ob.name]['size'],'sRGB')
        MAPS[ob.name]['BaseColor']=base;target_image(ob,base)
        isolate_bake([ob]);bpy.ops.object.bake(type='DIFFUSE',pass_filter={'COLOR'},use_selected_to_active=False)
        save_image(base)
        for m in ob.data.materials:
            nt=m.node_tree;p=next(n for n in nt.nodes if n.type=='BSDF_PRINCIPLED')
            for link in list(p.inputs['Base Color'].links):nt.links.remove(link)
            atlas=nt.nodes.new('ShaderNodeTexImage');atlas.name='BAKED_BASE_COLOR';atlas.image=base
            nt.links.new(atlas.outputs['Color'],p.inputs['Base Color'])
            # Unused procedural nodes remain as editable source, connected shader is exportable.
            nt.nodes['BAKE_TARGET'].image=base
    for ob in bpy.context.scene.objects:
        if ob.type=='MESH':ob.hide_render=ob in HIGH.values()
    record('材质',maps='Five BaseColor atlases plus the earlier Normal and AO atlases',
           surfaces='Cloth sheen, leather, wood, brass and steel use separate PBR parameters',
           texture_count=sum(1 for im in bpy.data.images if im.filepath and str(TEX) in im.filepath))


def stage_skin():
    count=0
    for ob in LOW:
        for m in ob.data.materials:
            family=m.get('surface_family','')
            p=next((n for n in m.node_tree.nodes if n.type=='BSDF_PRINCIPLED'),None)
            if family in ['Skin','SkinLight','EarBlush']:
                p.inputs['Subsurface Weight'].default_value=.10
                p.inputs['Subsurface Radius'].default_value=(1,.43,.20)
                p.inputs['Subsurface Scale'].default_value=.012
                p.inputs['Roughness'].default_value=.58
                p.inputs['Specular IOR Level'].default_value=.30
                count+=1
            elif family in ['EyeWhite','Iris','Pupil']:
                p.inputs['Roughness'].default_value=.23;p.inputs['Coat Weight'].default_value=.12
    record('皮肤 Shader',skin_materials=count,shader='Principled BSDF subsurface',
           subsurface_weight=.10,subsurface_scale_m=.012,radius_rgb=[1,.43,.20],
           limitations='Blender shader only; UE skin material and subsurface profile are not authored.')


def stage_hair():
    hair=bpy.data.objects['Windward_Hair']
    for m in hair.data.materials:
        p=next(n for n in m.node_tree.nodes if n.type=='BSDF_PRINCIPLED')
        p.inputs['Roughness'].default_value=.49;p.inputs['Anisotropic'].default_value=.35
        p.inputs['Sheen Weight'].default_value=.18;p.inputs['Sheen Roughness'].default_value=.55
        tangent=m.node_tree.nodes.new('ShaderNodeTangent');tangent.direction_type='UV_MAP';tangent.uv_map='UV0_Windward'
        m.node_tree.links.new(tangent.outputs['Tangent'],p.inputs['Tangent'])
    record('毛发',representation='Stylized solid polygon locks and hair cap; independent mesh',
           shading='Baked hair atlas, anisotropic highlight and sheen',
           limitations='No strand groom or hair simulation; head-bone attachment is created in the next stage.')


def stage_rig():
    global RIG
    scene=bpy.context.scene;char=bpy.data.collections['CHARACTER | skinned geometry']
    data=bpy.data.armatures.new('Windward_Armature');RIG=bpy.data.objects.new('Windward_Rig',data);char.objects.link(RIG)
    select([RIG]);bpy.ops.object.mode_set(mode='EDIT')
    for spec in SPECS:
        b=data.edit_bones.new(spec['name']);b.head=spec['head'];b.tail=spec['tail'];b.use_deform=spec['deform']
        b.align_roll(Vector((0,-1,0)))
        if spec['parent']:b.parent=data.edit_bones[spec['parent']]
    bpy.ops.object.mode_set(mode='OBJECT')
    deform=data.collections.new('Deform | body, fingers, cloth');control=data.collections.new('Controls | optional leg IK')
    for b in data.bones:(deform if b.use_deform else control).assign(b)
    RIG.show_in_front=True;data.display_type='OCTAHEDRAL'
    normalized=0;influences=0
    for ob in LOW:
        for i,weights in enumerate(SEEDS[ob.name]):
            total=sum(weights.values())
            if total<=0:raise RuntimeError(f'Unassigned authored vertex {ob.name}:{i}')
            for name,w in weights.items():
                vg=ob.vertex_groups.get(name) or ob.vertex_groups.new(name=name)
                vg.add([i],w/total,'REPLACE');influences=max(influences,len(weights))
            normalized+=1
        ob.parent=RIG
        m=ob.modifiers.new('Windward final skin','ARMATURE');m.object=RIG;m.use_deform_preserve_volume=True
    for side in ['l','r']:
        c=RIG.pose.bones['calf_'+side].constraints.new('IK');c.name='Optional leg IK; demos use FK'
        c.target=RIG;c.subtarget='CTRL_foot_'+side;c.pole_target=RIG;c.pole_subtarget='CTRL_knee_'+side;c.chain_count=2;c.influence=0
    for pb in RIG.pose.bones:pb.rotation_mode='QUATERNION'
    RIG['usage']='Pose Mode: FK body and fingers. Optional leg IK constraint influence defaults to 0.'
    RIG['skeleton_compatibility']='Original skeleton; UE Manny/Quinn requires separate retargeting.'
    record('骨骼权重',bones=len(data.bones),deform_bones=sum(b.use_deform for b in data.bones),
           vertices_with_normalized_weights=normalized,max_influences=influences,
           skinning='Explicit authored weights; shoulder, elbow, knee and ankle blends, full articulated fingers')


def stage_animation():
    spec=importlib.util.spec_from_file_location('windward_author',ROOT/'Tools/BlenderMCP/build_windward_hero.py')
    author=importlib.util.module_from_spec(spec);spec.loader.exec_module(author)
    author.RIG=RIG;author.PARTS=LOW
    author.actions()
    record('动画',fps=30,actions=[{'action':n,'frames':e} for n,e in [('Idle_Breathe',61),('Walk_InPlace',31),('Wave_Hello',81),('Sword_Slash',41)]],
           limitations='Editable prototype motions; production foot lock, root motion and gameplay integration remain unfinished.')
    return author


def save_and_export(author):
    # Exports select LOW plus RIG; the high bake collection is preserved only in .blend.
    bpy.data.collections['HIGH | editable bake sources'].hide_viewport=True
    bpy.data.collections['HIGH | editable bake sources'].hide_render=True
    studio=bpy.data.collections['STUDIO | excluded from exports'];studio.hide_viewport=True
    for ob in LOW:ob.hide_render=False
    for im in bpy.data.images:
        if im.filepath and str(TEX) in im.filepath:
            im.filepath=bpy.path.relpath(im.filepath)
            im.pack()
    author.export_and_save()
    info=json.loads((OUT/'asset_manifest.json').read_text(encoding='utf-8'))
    info['workflow_report']='workflow_report.json'
    info['high_poly_vertices']=sum(len(o.data.vertices) for o in HIGH.values())
    info['textures']=[p.name for p in TEX.glob('*.png')]
    info['skin_shader']='Blender Principled subsurface; not transferred to UE'
    info['hair']='Stylized polygon locks; anisotropic material, no groom'
    info['limitations'].extend(['Automatic UV seams need production polish','High-poly detail is procedurally authored, not hand sculpted',
                               'UE material, import, retarget and runtime are not verified'])
    (OUT/'asset_manifest.json').write_text(json.dumps(info,ensure_ascii=False,indent=2),encoding='utf-8')
    notes=bpy.data.texts.get('READ_ME - rig and prototype scope')
    notes.write('\n\nFINISHING WORKFLOW: topology -> normals -> UV -> high-to-low normal/AO bake -> PBR -> skin subsurface -> polygon hair -> rig/normalized weights -> animation.\nEditable high meshes in hidden HIGH collection. Images packed; external PNGs in Textures.\nUV layouts in UV; actual stage report in workflow_report.json. No UE acceptance.\n')
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'WindwardHero.blend'))
    REPORT['delivery']={'blend':'WindwardHero.blend','texture_files':len(list(TEX.glob('*.png'))),
                        'high_vertices':info['high_poly_vertices'],'ue':'未编译、未测试'}
    (OUT/'workflow_report.json').write_text(json.dumps(REPORT,ensure_ascii=False,indent=2),encoding='utf-8')
    author.render_previews(bpy.context.scene.camera,studio)


if __name__=='__main__':
    stage_topology();stage_normals();stage_uv();stage_bake();stage_materials();stage_skin();stage_hair();stage_rig()
    author=stage_animation();save_and_export(author)
