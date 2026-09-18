"""Update only the basic sword cutting profile after compiling native reaction changes."""
import unreal as ue
asset=ue.EditorAssetLibrary.load_asset('/Game/AetherCore/Data/DA_TrainingSword')
assert asset, 'Basic sword asset missing'
attacks=list(asset.get_editor_property('attacks'))
assert {str(a.id) for a in attacks}=={'Light','Heavy'}
for attack in attacks:attack.set_editor_property('cutting_work_j',24.0 if str(attack.id)=='Light' else 40.0)
asset.set_editor_property('attacks',attacks)
assert ue.EditorAssetLibrary.save_loaded_asset(asset)
print('AETHER_CUTTING_ASSET_PASS')
