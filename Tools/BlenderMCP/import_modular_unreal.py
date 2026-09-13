"""UE 5.8 editor import + native data assets; no generated Blueprint combat logic."""
import unreal as ue,json,traceback
from pathlib import Path
ROOT=Path('C:/ueproject/test');ART=ROOT/'Art/SwordMagic/Modular';PACK='/Game/SwordMagic'
MAN=json.loads((ART/'manifest.json').read_text(encoding='utf-8'))
LIB=ue.EditorAssetLibrary;TOOLS=ue.AssetToolsHelpers.get_asset_tools();ME=ue.MaterialEditingLibrary
REPORT={'stage':'starting','assets':[],'errors':[]}
def checkpoint(stage):
    REPORT['stage']=stage;(ART/'unreal-import-report.json').write_text(json.dumps(REPORT,indent=2),encoding='utf-8')
def props(ob,**values):
    for key,value in values.items():ob.set_editor_property(key,value)
    return ob
def create(name,cls,folder='Data'):
    path=PACK+'/'+folder+'/'+name
    if LIB.does_asset_exist(path):return LIB.load_asset(path)
    if cls==ue.Material:factory=ue.MaterialFactoryNew()
    elif cls==ue.MaterialInstanceConstant:factory=ue.MaterialInstanceConstantFactoryNew()
    else:factory=ue.DataAssetFactory();factory.set_editor_property('data_asset_class',cls)
    return TOOLS.create_asset(name,PACK+'/'+folder,cls,factory)
def material_pack():
    master=create('M_SwordMagic',ue.Material,'Materials');ME.delete_all_material_expressions(master)
    def expr(cls,x,y):return ME.create_material_expression(master,cls,x,y)
    def scalar(name,value,x,y):
        o=expr(ue.MaterialExpressionScalarParameter,x,y);o.set_editor_property('parameter_name',name);o.set_editor_property('default_value',value);return o
    def vector(name,value,x,y):
        o=expr(ue.MaterialExpressionVectorParameter,x,y);o.set_editor_property('parameter_name',name);o.set_editor_property('default_value',ue.LinearColor(*value,1));return o
    def wire(a,b,p):ME.connect_material_expressions(a,'',b,p)
    base=vector('BaseColor',(.4,.4,.4),-650,0);burn=scalar('BurnAmount',0,-650,180);frost=scalar('FrostAmount',0,-380,180)
    burned=expr(ue.MaterialExpressionLinearInterpolate,-380,0);wire(base,burned,'A');burned.set_editor_property('const_b',.025);wire(burn,burned,'Alpha')
    frosted=expr(ue.MaterialExpressionLinearInterpolate,-140,0);wire(burned,frosted,'A');ice=vector('FrostColor',(.25,.72,.83),-380,320);wire(ice,frosted,'B');wire(frost,frosted,'Alpha')
    ME.connect_material_property(frosted,'',ue.MaterialProperty.MP_BASE_COLOR)
    rough=scalar('Roughness',.65,-650,500);wet=scalar('Wetness',0,-650,640)
    roughlerp=expr(ue.MaterialExpressionLinearInterpolate,-300,500);wire(rough,roughlerp,'A');roughlerp.set_editor_property('const_b',.16);wire(wet,roughlerp,'Alpha');ME.connect_material_property(roughlerp,'',ue.MaterialProperty.MP_ROUGHNESS)
    metal=scalar('Metallic',0,-300,720);ME.connect_material_property(metal,'',ue.MaterialProperty.MP_METALLIC)
    glow=scalar('EmissionStrength',0,-650,-250);mult=expr(ue.MaterialExpressionMultiply,-200,-250);wire(base,mult,'A');wire(glow,mult,'B');ME.connect_material_property(mult,'',ue.MaterialProperty.MP_EMISSIVE_COLOR)
    ME.set_base_material_usage(master,ue.MaterialUsage.MATUSAGE_SKELETAL_MESH)
    ME.recompile_material(master);LIB.save_loaded_asset(master)
    mats={}
    for name,m in MAN['materials'].items():
        mi=create(name.replace('M_SM_','MI_'),ue.MaterialInstanceConstant,'Materials');ME.set_material_instance_parent(mi,master)
        ME.set_material_instance_vector_parameter_value(mi,'BaseColor',ue.LinearColor(*m['color'],1))
        for key,value in [('Metallic',m['metallic']),('Roughness',m['roughness']),('EmissionStrength',m['emission'])]:ME.set_material_instance_scalar_parameter_value(mi,key,value)
        ME.update_material_instance(mi);LIB.save_loaded_asset(mi);mats[name]=mi
    return mats
def import_fbx(name,folder,kind='static',skeleton=None):
    existing=PACK+'/'+folder+'/'+name
    if 'AetherDataOnly' in ue.SystemLibrary.get_command_line() and LIB.does_asset_exist(existing) and 'Apprentice' not in name:
        result=LIB.load_asset(existing);REPORT['assets'].append(result.get_path_name());return result
    task=ue.AssetImportTask();task.filename=str(ART/'FBX'/f'{name}.fbx');task.destination_path=PACK+'/'+folder;task.destination_name=name
    task.automated=True;task.replace_existing=True;task.save=True
    options=ue.FbxImportUI()
    for key,value in [('import_mesh',kind!='animation'),('import_as_skeletal',kind!='static'),('import_materials',False),('import_textures',False),('import_animations',kind=='animation'),('automated_import_should_detect_type',False),('create_physics_asset',False)]:options.set_editor_property(key,value)
    options.mesh_type_to_import=ue.FBXImportType.FBXIT_STATIC_MESH if kind=='static' else ue.FBXImportType.FBXIT_ANIMATION if kind=='animation' else ue.FBXImportType.FBXIT_SKELETAL_MESH
    if skeleton:options.skeleton=skeleton
    data=options.static_mesh_import_data if kind=='static' else options.anim_sequence_import_data if kind=='animation' else options.skeletal_mesh_import_data
    for key,value in [('convert_scene',True),('convert_scene_unit',True),('force_front_x_axis',False),('import_uniform_scale',1.0)]:data.set_editor_property(key,value)
    if kind=='static':
        for key,value in [('combine_meshes',True),('auto_generate_collision',name!='SM_Abbey_StaticShell'),('generate_lightmap_u_vs',True)]:data.set_editor_property(key,value)
    if kind!='animation':data.normal_import_method=ue.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS
    task.options=options;task.factory=ue.FbxFactory();TOOLS.import_asset_tasks([task])
    cls=ue.StaticMesh if kind=='static' else ue.AnimSequence if kind=='animation' else ue.SkeletalMesh
    result=next((a for a in task.get_objects() if isinstance(a,cls)),None)
    if result is None:raise RuntimeError('No imported '+kind+' for '+name+':'+str(task.imported_object_paths))
    # FBX task.save persists the principal mesh, but not its separately created
    # USkeleton in this import path. Persist dependencies before animation import.
    if kind=='skeletal':
        skel=result.get_editor_property('skeleton')
        if not skel:raise RuntimeError('Skeletal mesh has no skeleton: '+name)
        LIB.save_loaded_asset(skel,only_if_is_dirty=False)
        REPORT['assets'].append(skel.get_path_name())
    LIB.save_loaded_asset(result,only_if_is_dirty=False)
    REPORT['assets'].append(result.get_path_name());return result
def set_materials(asset,mats,skeletal=False):
    slots=asset.get_editor_property('materials' if skeletal else 'static_materials')
    for i,slot in enumerate(slots):
        key=str(slot.get_editor_property('imported_material_slot_name' if skeletal else 'material_slot_name'))
        if key not in mats:key=str(slot.get_editor_property('material_slot_name'))
        if key in mats:
            if skeletal:slot.set_editor_property('material_interface',mats[key]);slots[i]=slot
            else:asset.set_material(i,mats[key])
        else:REPORT['errors'].append(asset.get_name()+': unmapped material '+key)
    if skeletal:asset.materials=slots
    LIB.save_loaded_asset(asset,only_if_is_dirty=False)
    if skeletal:REPORT.setdefault('character_materials',{})[asset.get_name()]=[str(s.material_interface.get_path_name()) if s.material_interface else None for s in asset.materials]
def tr(loc=(0,0,0),yaw=0,scale=(1,1,1)):
    return ue.Transform(location=ue.Vector(*loc),rotation=ue.Rotator(pitch=0,yaw=yaw,roll=0),scale=ue.Vector(*scale))
def eqslot(slot,item):
    s=ue.AetherEquippedSlot();s.slot=slot;s.item_id=item;return s
def attack(id,damage,posture,impulse,cost,reach,windup,active,recovery):
    a=ue.AetherAttackDefinition();a.id=id;a.damage=damage;a.posture_damage=posture;a.impulse_ns=impulse;a.stamina_cost=cost;a.reach_cm=reach;a.radius_cm=52;a.windup_seconds=windup;a.active_seconds=active;a.recovery_seconds=recovery;return a
def game_data(meshes,chars,animations):
    defs=[]
    for id,label,mesh,slot,socket,two,scale in [('OathSword','Oath Sword','SM_OathSword','MainHand','hand_r',False,1),('OathShield','Oath Shield','SM_OathShield','OffHand','hand_l',False,1),('TrainingHammer','War Hammer','SM_BellHammer','MainHand','hand_r',True,.58),('BellHammer','Bell Hammer','SM_BellHammer','MainHand','hand_r',True,1)]:
        d=create('DA_'+id,ue.AetherEquipmentDefinition);props(d,item_id=id,display_name=label,mesh=meshes[mesh],slot=slot,socket=socket,occupies_both_hands=two,allows_guard=slot=='OffHand',grip_transform=tr(scale=(scale,)*3))
        d.set_editor_property('attacks',[] if slot=='OffHand' else [attack('Light',16,14,6,8,165,.08,.12,.18),attack('Heavy',32,45,32,24,165,.18,.15,.42)] if id=='OathSword' else [attack('Light',28,30,20,16,220,.20,.16,.44),attack('Heavy',45,65,50,32,245,.30,.18,.57)])
        LIB.save_loaded_asset(d);defs.append(d)
    catalog=create('DA_EquipmentCatalog',ue.AetherEquipmentCatalog);catalog.set_editor_property('items',defs);LIB.save_loaded_asset(catalog)
    profiles={}
    for id,body,hh,loadout in [('Player','Oathwanderer',88,[eqslot('MainHand','OathSword'),eqslot('OffHand','OathShield')]),('Guard','Oathwanderer',88,[eqslot('MainHand','OathSword'),eqslot('OffHand','OathShield')]),('Caster','Oathwanderer',88,[]),('Boss','BellKnight_Auren',164,[eqslot('MainHand','BellHammer')])]:
        d=create('DA_Character_'+id,ue.AetherCharacterDefinition);props(d,character_id=id,body_mesh=chars[body],walk_animation=animations[body+'_Walk'],attack_animation=animations[body+'_Attack'],capsule_half_height=hh,capsule_radius=34 if id!='Boss' else 52,initial_equipment=loadout,quick_equip_items=['OathSword','TrainingHammer'] if id=='Player' else [])
        LIB.save_loaded_asset(d);profiles[id]=d
    level=create('DA_BrokenBellAbbey',ue.AetherLevelDefinition);props(level,layout_id='BrokenBell_Modular_v1',static_shell=meshes['SM_Abbey_StaticShell'],shell_transform=tr((1650,0,0),90),player_spawn=tr((-850,0,110)),court_entry_x=450,arena_entry_x=3550)
    objects=[]
    def obj(id,kind,loc,scale=(1,1,1),mesh=None,water=0,label='',enabled=True,yaw=0):
        s=ue.AetherObjectSpec();s.id=id;s.label=label;s.kind=getattr(ue.AetherObjectKind,kind);s.scale=ue.Vector(*scale);s.interactive_material=kind in ('TIMBER','ROPE','WATER','OIL','CISTERN','SIGIL')
        if mesh:s.art_mesh=mesh
        color=(.03,.22,.36) if kind in ('WATER','CISTERN') else (.3,.14,.045) if kind in ('TIMBER','ROPE','BRIDGE') else (.7,.5,.13)
        s.color=ue.LinearColor(*color,1);e=ue.AetherLevelObject();e.spec=s;e.transform=tr(loc,yaw);e.initial_water_kg=water;e.initially_enabled=enabled;objects.append(e)
    def coord(x,y,z):return (y*100+1650,x*100,z*100)
    # Independent source meshes are centred at their authored positions.
    for entry in MAN['level']:
        n=entry['name'];loc=coord(*entry['location_m']);mesh=meshes[n]
        if n=='SM_Abbey_StaticShell':continue
        if 'BridgePlank' in n:obj('Original_'+n,'TIMBER',loc,mesh=mesh,yaw=90)
        elif 'CoverCrate' in n:obj('Crate_'+n,'TIMBER',loc,mesh=mesh,yaw=90)
        elif 'AncientSigil' in n:obj('Sigil','SIGIL',loc,mesh=mesh,yaw=90,label='ANCIENT SIGIL / E KEEP / Q CARRY')
        elif 'WitnessBell' in n:obj('Witness','WITNESS',loc,mesh=mesh,yaw=90,label='WITNESS BELL / E RELEASE OATH')
        elif 'OathRecordPedestal' in n:obj('RecordPedestal','STONE',loc,mesh=mesh,yaw=90)
        elif 'OathRecord' in n:obj('Record','RECORD',loc,mesh=mesh,yaw=90,label='OATH RECORD / E READ')
        elif 'ApprenticeCloak' in n:obj('Apprentice','APPRENTICE',loc,mesh=mesh,yaw=90,label='APPRENTICE / E RESCUE')
        elif 'Apprentice' in n:obj('Decor_'+n,'STONE',loc,mesh=mesh,yaw=90)
        elif 'CisternWater' in n:obj('WaterWell','CISTERN',loc,mesh=mesh,water=8,yaw=90,label='CISTERN / E FILL WATER')
        # Water and rope are covered below by explicit simulation cells and authoring proxies.
    obj('Rope','ROPE',coord(-1.7,-19.6,1.1),(.22,.22,2.2),label='CUT BRIDGE ROPE / HEAVY STRIKE OR HEAT')
    for i in range(3):
        obj('Bridge'+str(i),'BRIDGE',coord(0,-17.5+i*.5,.17),(.5,3.5,.19),enabled=False)
        obj('IcePath'+str(i),'WATER',coord(6,-18+i*1.5,-.2),(1.5,3,.15),water=.5,label='SHALLOW WATER / FROST TO CROSS' if i==0 else '')
    obj('EntryMarker','RETURN',coord(-4,-26,.8),(.5,.5,1.6),label='GREYFORD / E RETURN')
    obj('CourtWater0','WATER',coord(4,-5,.12),(3,3,.15),water=.7,label='CONDUCTIVE WATER / LIGHTNING')
    obj('CourtWater1','WATER',coord(6.8,-5,.12),(3,3,.15),water=.4)
    obj('CourtBeam','TIMBER',coord(-8,-4,1.2),(.4,.4,2.4),label='BURNABLE TIMBER')
    obj('OilBowl','OIL',coord(-7,-4,.6),(.8,.8,1.2),label='LAMP OIL')
    for i in range(4):obj('ArenaWater'+str(i),'WATER',coord(-3+i*2.6,25,1.15),(3,2.6,.15),water=.8 if i==0 else 0)
    obj('ArenaCistern','CISTERN',coord(-7,24,1.65),(1,1,1.1),water=6,label='SLUICE / E RELEASE WATER')
    level.set_editor_property('objects',objects);enemies=[]
    for id,archetype,loc,p in [('Guard',1,coord(5,-3,1),'Guard'),('Caster',2,coord(-6,0,1),'Caster'),('Olen',3,coord(0,27,2.8),'Boss')]:
        e=ue.AetherLevelEnemy();e.id=id;e.archetype=archetype;e.transform=tr(loc,180);e.definition=profiles[p];enemies.append(e)
    level.set_editor_property('enemies',enemies);LIB.save_loaded_asset(level)
    content=create('DA_GameContent',ue.AetherGameContent);props(content,equipment_catalog=catalog,abbey=level,player=profiles['Player'],guard=profiles['Guard'],caster=profiles['Caster'],boss=profiles['Boss']);LIB.save_loaded_asset(content)
    REPORT['equipment_definitions']=[d.get_path_name() for d in defs];REPORT['level_objects']=len(objects);REPORT['content']=content.get_path_name()
try:
    checkpoint('materials');mats=material_pack();meshes={};chars={};animations={}
    ue.SystemLibrary.execute_console_command(None,'Interchange.FeatureFlags.Import.FBX 0')
    checkpoint('static_meshes')
    for entry in MAN['equipment']+MAN['level']:
        sm=import_fbx(entry['name'],'Equipment' if entry in MAN['equipment'] else 'Environment');set_materials(sm,mats)
        if entry['name']=='SM_Abbey_StaticShell':
            body=sm.get_editor_property('body_setup');body.set_editor_property('collision_trace_flag',ue.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE);LIB.save_loaded_asset(sm)
        meshes[entry['name']]=sm
    checkpoint('skeletal_meshes')
    for name in ('Oathwanderer','BellKnight_Auren'):
        sk=import_fbx('SK_Modular_'+name,'Characters','skeletal');set_materials(sk,mats,True);chars[name]=sk
        for kind in ('Walk','Attack'):animations[name+'_'+kind]=import_fbx('AN_'+name+'_'+kind,'Animations','animation',sk.get_editor_property('skeleton'))
    checkpoint('native_data_assets');game_data(meshes,chars,animations)
    map_path=PACK+'/Maps/L_BrokenBell_Playable'
    editor=ue.get_editor_subsystem(ue.LevelEditorSubsystem)
    if not LIB.does_asset_exist(map_path):
        if not editor.new_level(map_path):raise RuntimeError('Could not create playable map')
        world=ue.EditorLevelLibrary.get_editor_world();world.get_world_settings().set_editor_property('default_game_mode',ue.AetherModularAdventureMode)
        editor.save_current_level()
    REPORT['playable_map']=map_path
    if REPORT['errors']:raise RuntimeError('; '.join(REPORT['errors']))
    checkpoint('complete')
except Exception:
    REPORT['errors'].append(traceback.format_exc());checkpoint('failed');raise
