"""Import the original AetherLab pack and create a separate asset-preview map.

UnrealEditor-Cmd AetherLab.uproject -EnablePlugins=PythonScriptPlugin,EditorScriptingUtilities
  -run=pythonscript -script=.../import_unreal.py -unattended -NullRHI
No gameplay C++ or existing maps are modified.
"""
import unreal as ue
import json
import traceback
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
ART=ROOT/'Art/AetherLab'
PACK='/Game/AetherLabPrototype'
MANIFEST=json.loads((ART/'asset-manifest.json').read_text(encoding='utf-8'))
REPORT={'stage':'starting','meshes':[],'errors':[],'materials':[],'map':None}
TOOLS=ue.AssetToolsHelpers.get_asset_tools()
LIB=ue.EditorAssetLibrary
ME=ue.MaterialEditingLibrary
SME=ue.get_editor_subsystem(ue.StaticMeshEditorSubsystem) or ue.StaticMeshEditorSubsystem()

def checkpoint(stage):
    REPORT['stage']=stage
    (ART/'unreal-import-report.json').write_text(json.dumps(REPORT,ensure_ascii=False,indent=2),encoding='utf-8')
    ue.log('AETHER_IMPORT '+stage)

def create(name,folder,cls,factory):
    path=folder+'/'+name
    return LIB.load_asset(path) if LIB.does_asset_exist(path) else TOOLS.create_asset(name,folder,cls,factory)

def material_pack():
    master=create('M_Aether_Surface',PACK+'/Materials',ue.Material,ue.MaterialFactoryNew())
    ME.delete_all_material_expressions(master)
    def expr(cls,x,y):return ME.create_material_expression(master,cls,x,y)
    def scalar(name,value,x,y):
        e=expr(ue.MaterialExpressionScalarParameter,x,y);e.set_editor_property('parameter_name',name);e.set_editor_property('default_value',value);return e
    def vector(name,value,x,y):
        e=expr(ue.MaterialExpressionVectorParameter,x,y);e.set_editor_property('parameter_name',name);e.set_editor_property('default_value',ue.LinearColor(*value,1));return e
    def wire(a,b,pin):ME.connect_material_expressions(a,'',b,pin)
    base=vector('BaseColor',(.2,.3,.3),-900,0)
    metallic=scalar('Metallic',0,-500,350)
    rough=scalar('Roughness',.6,-900,480)
    wet=scalar('Wetness',0,-900,650)
    burn=scalar('BurnAmount',0,-650,200)
    frost=scalar('FrostAmount',0,-340,240)
    emission=scalar('EmissionStrength',0,-350,-270)
    # Local position keeps grain attached to moving objects.
    tex=expr(ue.MaterialExpressionTextureCoordinate,-1150,-250)
    noise=expr(ue.MaterialExpressionNoise,-900,-250)
    noise.set_editor_property('scale',7.0);noise.set_editor_property('quality',1)
    # UV0 expanded into 3-vector for inexpensive scalar noise.
    append=expr(ue.MaterialExpressionAppendVector,-1000,-250)
    zero=expr(ue.MaterialExpressionConstant,-1200,-100);zero.set_editor_property('r',0)
    wire(tex,append,'A');wire(zero,append,'B');wire(append,noise,'Position')
    mult=expr(ue.MaterialExpressionMultiply,-650,-100);wire(base,mult,'A')
    variation=expr(ue.MaterialExpressionMultiply,-740,-270);wire(noise,variation,'A');variation.set_editor_property('const_b',.35)
    offset=expr(ue.MaterialExpressionAdd,-650,-250);wire(variation,offset,'A');offset.set_editor_property('const_b',.72);wire(offset,mult,'B')
    burned=expr(ue.MaterialExpressionLinearInterpolate,-420,0);wire(mult,burned,'A');burned.set_editor_property('const_b',.018);wire(burn,burned,'Alpha')
    frosted=expr(ue.MaterialExpressionLinearInterpolate,-150,0);wire(burned,frosted,'A')
    ice=vector('FrostColor',(.4,.72,.8),-400,120);wire(ice,frosted,'B');wire(frost,frosted,'Alpha')
    ME.connect_material_property(frosted,'',ue.MaterialProperty.MP_BASE_COLOR)
    wetrough=expr(ue.MaterialExpressionLinearInterpolate,-400,500);wire(rough,wetrough,'A');wetrough.set_editor_property('const_b',.12);wire(wet,wetrough,'Alpha')
    ME.connect_material_property(wetrough,'',ue.MaterialProperty.MP_ROUGHNESS)
    ME.connect_material_property(metallic,'',ue.MaterialProperty.MP_METALLIC)
    glow=expr(ue.MaterialExpressionMultiply,-50,-200);wire(base,glow,'A');wire(emission,glow,'B')
    ME.connect_material_property(glow,'',ue.MaterialProperty.MP_EMISSIVE_COLOR)
    master.set_editor_property('two_sided',False)
    ME.layout_material_expressions(master);ME.recompile_material(master);LIB.save_loaded_asset(master)
    mats={}
    for name,entry in MANIFEST['palette'].items():
        mi=create('MI_Aether_'+name,PACK+'/Materials',ue.MaterialInstanceConstant,ue.MaterialInstanceConstantFactoryNew())
        ME.set_material_instance_parent(mi,master)
        ME.set_material_instance_vector_parameter_value(mi,'BaseColor',ue.LinearColor(*entry['color'],1))
        for key,value in [('Metallic',entry['metallic']),('Roughness',entry['roughness']),('EmissionStrength',entry['emissive'])]:
            ME.set_material_instance_scalar_parameter_value(mi,key,value)
        ME.update_material_instance(mi);LIB.save_loaded_asset(mi);mats['M_Aether_'+name]=mi
        REPORT['materials'].append(mi.get_path_name())
    return mats

def import_meshes(mats):
    tasks=[]
    for entry in MANIFEST['assets']:
        task=ue.AssetImportTask();task.set_editor_property('filename',str(ART/entry['fbx']))
        task.set_editor_property('destination_path',PACK+'/Meshes/'+entry['category'])
        task.set_editor_property('destination_name',entry['name']);task.set_editor_property('automated',True)
        task.set_editor_property('replace_existing',True);task.set_editor_property('save',True)
        options=ue.FbxImportUI()
        for key,value in [('import_mesh',True),('import_as_skeletal',False),('import_materials',False),
                          ('import_textures',False),('import_animations',False),('automated_import_should_detect_type',False)]:options.set_editor_property(key,value)
        options.set_editor_property('mesh_type_to_import',ue.FBXImportType.FBXIT_STATIC_MESH)
        data=options.get_editor_property('static_mesh_import_data')
        for key,value in [('combine_meshes',True),('auto_generate_collision',False),('one_convex_hull_per_ucx',True),
                          ('generate_lightmap_u_vs',True),('convert_scene',True),('convert_scene_unit',True),
                          ('force_front_x_axis',False),('import_uniform_scale',1.0)]:data.set_editor_property(key,value)
        data.set_editor_property('normal_import_method',ue.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS)
        task.set_editor_property('options',options);task.set_editor_property('factory',ue.FbxFactory())
        tasks.append(task)
    # Explicit FbxFactory and disabled Interchange keep UCX semantics predictable.
    ue.SystemLibrary.execute_console_command(None,'Interchange.FeatureFlags.Import.FBX 0')
    TOOLS.import_asset_tasks(tasks)
    meshes={}
    for entry,task in zip(MANIFEST['assets'],tasks):
        results=[a for a in task.get_objects() if isinstance(a,ue.StaticMesh)]
        if not results:raise RuntimeError('No StaticMesh imported for '+entry['name'])
        sm=results[0];meshes[entry['name']]=sm
        for i,slot in enumerate(sm.get_editor_property('static_materials')):
            slot_name=str(slot.get_editor_property('material_slot_name'))
            material=mats.get(slot_name)
            if material is None and i<len(entry['material_slots']): material=mats.get(entry['material_slots'][i])
            if material:sm.set_material(i,material)
            else:REPORT['errors'].append('Unmapped material '+entry['name']+':'+slot_name)
        LIB.set_metadata_tag(sm,'AetherPreset',entry['preset'])
        LIB.set_metadata_tag(sm,'SourceModel',entry['fbx'])
        LIB.set_metadata_tag(sm,'Usage',entry['notes'])
        LIB.save_loaded_asset(sm)
        bounds=sm.get_bounds();ext=bounds.box_extent
        dims=[round(ext.x*2,3),round(ext.y*2,3),round(ext.z*2,3)]
        expected=[v*100 for v in entry['dimensions_m']]
        if max(abs(a-b) for a,b in zip(sorted(dims),sorted(expected)))>max(1,max(expected)*.015):
            REPORT['errors'].append('Scale mismatch '+entry['name']+': '+str(dims)+' expected '+str(expected))
        collisions=SME.get_convex_collision_count(sm)
        uv_channels=SME.get_num_uv_channels(sm,0)
        if uv_channels<2: REPORT['errors'].append('Missing UV or lightmap channel '+entry['name'])
        if entry['collision_hulls']>0 and collisions==0: REPORT['errors'].append('Missing collision '+entry['name'])
        if entry['fluid'] and SME.get_simple_collision_count(sm)>0: REPORT['errors'].append('Unexpected fluid collision '+entry['name'])
        REPORT['meshes'].append({'name':entry['name'],'path':sm.get_path_name(),'dimensions_cm':dims,
            'convex_collision_count':collisions,'uv_channels':uv_channels,'material_slots':sm.get_num_sections(0)})
    return meshes

def preview_map(meshes):
    editor=ue.get_editor_subsystem(ue.LevelEditorSubsystem) or ue.LevelEditorSubsystem()
    map_path=PACK+'/Maps/L_AetherLab_AssetPreview'
    if LIB.does_asset_exist(map_path):
        if not editor.load_level(map_path):raise RuntimeError('Could not reopen asset preview map')
        actors=ue.get_editor_subsystem(ue.EditorActorSubsystem) or ue.EditorActorSubsystem()
        placed=[a for a in actors.get_all_level_actors() if isinstance(a,ue.StaticMeshActor)]
        REPORT['map']=map_path
        REPORT['placed_mesh_actors']=len(placed)
        # Asset references update after reimport; preserve subsequent user edits to the map.
        return
    world=editor.new_level(map_path)
    if not world:raise RuntimeError('Could not create asset preview map')
    actors=ue.get_editor_subsystem(ue.EditorActorSubsystem) or ue.EditorActorSubsystem()
    by_name={e['name']:e for e in MANIFEST['assets']}
    for i,item in enumerate(MANIFEST['placements']):
        x,y,z=item['location_m']
        actor=actors.spawn_actor_from_class(ue.StaticMeshActor,ue.Vector(x*100,-y*100,z*100),ue.Rotator(0,-item['yaw_degrees'],0))
        actor.set_actor_label(f'{i:02d}_'+item['asset'][3:])
        actor.set_folder_path('AetherLab/'+by_name[item['asset']]['category'])
        comp=actor.static_mesh_component;comp.set_static_mesh(meshes[item['asset']])
        actor.set_actor_scale3d(ue.Vector(*item['scale']))
        comp.set_mobility(ue.ComponentMobility.STATIC)
        if by_name[item['asset']]['fluid']:comp.set_collision_enabled(ue.CollisionEnabled.NO_COLLISION)
    sun=actors.spawn_actor_from_class(ue.DirectionalLight,ue.Vector(0,0,1000),ue.Rotator(-50,-40,0))
    sun.set_actor_label('Preview sun');sun.light_component.set_editor_property('intensity',3.0)
    sky=actors.spawn_actor_from_class(ue.SkyLight,ue.Vector(0,0,800));sky.light_component.set_editor_property('intensity',1.3)
    sky.light_component.set_editor_property('real_time_capture',True)
    actors.spawn_actor_from_class(ue.SkyAtmosphere,ue.Vector(0,0,0))
    player=actors.spawn_actor_from_class(ue.PlayerStart,ue.Vector(0,480,110),ue.Rotator(0,-90,0))
    player.set_actor_label('Preview player start')
    camera=actors.spawn_actor_from_class(ue.CameraActor,ue.Vector(1800,2400,2200),ue.Rotator(0,0,0))
    camera.set_actor_rotation(ue.MathLibrary.find_look_at_rotation(camera.get_actor_location(),ue.Vector(0,-30,45)),False)
    camera.set_actor_label('Asset overview camera')
    editor.save_current_level()
    REPORT['map']=PACK+'/Maps/L_AetherLab_AssetPreview'
    REPORT['placed_mesh_actors']=len(MANIFEST['placements'])

try:
    checkpoint('materials');mats=material_pack()
    checkpoint('importing');meshes=import_meshes(mats)
    checkpoint('preview_map');preview_map(meshes)
    checkpoint('complete' if not REPORT['errors'] else 'validation_failed')
    if REPORT['errors']: raise RuntimeError('; '.join(REPORT['errors']))
except Exception:
    REPORT['errors'].append(traceback.format_exc());checkpoint('failed');raise
