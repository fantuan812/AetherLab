import unreal as ue
levels=ue.get_editor_subsystem(ue.LevelEditorSubsystem)
assert levels.load_level('/Game/AetherCore/Maps/L_Frontier')
world=ue.get_editor_subsystem(ue.UnrealEditorSubsystem).get_editor_world()
settings=world.get_world_settings()
ue.log('AETHER_NAV_CONFIG '+str(settings.get_editor_property('navigation_system_config')))
for a in ue.get_editor_subsystem(ue.EditorActorSubsystem).get_all_level_actors():
 if isinstance(a,(ue.NavMeshBoundsVolume,ue.RecastNavMesh)):
  ue.log('AETHER_NAV_ACTOR '+a.get_class().get_name()+' '+str(a.get_actor_bounds(False))+' spatial='+str(a.get_editor_property('is_spatially_loaded')))
