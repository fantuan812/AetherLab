import unreal as ue
path='/Game/AetherCore/Maps/L_Frontier'
levels=ue.get_editor_subsystem(ue.LevelEditorSubsystem)
if ue.EditorAssetLibrary.does_asset_exist(path):
    assert levels.load_level(path), 'Cannot load partition map'
else:
    assert levels.new_level(path, True), 'Cannot create partition map'
world=ue.get_editor_subsystem(ue.UnrealEditorSubsystem).get_editor_world()
assert ue.AetherWorldAuthoring.bake_static_shell(world), 'Partition shell failed'
assert levels.save_current_level(), 'Level save failed'
assert ue.EditorLoadingAndSavingUtils.save_dirty_packages(True, True), 'External actor save failed'

for a in ue.get_editor_subsystem(ue.EditorActorSubsystem).get_all_level_actors():
 if isinstance(a, ue.NavMeshBoundsVolume):
  origin, extent=a.get_actor_bounds(False)
  assert extent.x>40000 and extent.y>40000 and extent.z>1000, 'Empty navigation bounds'
  ue.log('AETHER_NAV_BOUNDS '+str(extent))
ue.log('AETHER_PARTITION_MAP_CREATED')
