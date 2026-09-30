"""Strict, source-hash-bound two-hand authoring contract. No positional legacy conversion.

No production rows are supplied: restore and review the original grip manifest first.
Transforms are explicitly authored in imported Unreal local frames, centimetres and xyzw.
"""
import hashlib
import json
import math
from pathlib import Path
import re

PACKAGE = re.compile(r'^/(?:Game|Engine)/(?:[A-Za-z0-9_]+/)*[A-Za-z0-9_]+$')
NAME = re.compile(r'^[A-Za-z][A-Za-z0-9_]{0,63}$')
FIELDS = {'item_id', 'equipment_asset', 'target_mesh', 'weapon_mesh', 'main_bone',
          'support_bone', 'socket', 'weapon_to_socket', 'support_hand_to_weapon'}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def unique_pairs(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, 'Duplicate grip field: ' + key)
        result[key] = value
    return result


def transform(value, rigid=False):
    require(type(value) is dict and set(value) == {'translation_cm', 'rotation_xyzw', 'scale'},
            'Grip transform requires translation_cm, rotation_xyzw and scale')
    for key, count in [('translation_cm', 3), ('rotation_xyzw', 4), ('scale', 3)]:
        values = value[key]
        require(type(values) is list and len(values) == count and
                all(type(v) in (int, float) and math.isfinite(v) for v in values), 'Invalid ' + key)
    require(sum(v*v for v in value['translation_cm']) <= 1000**2, 'Grip translation exceeds 1000cm')
    require(abs(sum(v*v for v in value['rotation_xyzw']) - 1) <= 1e-4, 'Grip quaternion must be normalized')
    scale = value['scale']
    require(min(scale) > 1e-8 and max(abs(v-scale[0]) for v in scale) <= 1e-4,
            'Grip scale must be positive and uniform; bake nonuniform scale into the mesh')
    require(not rigid or max(abs(v-1) for v in scale) <= 1e-4, 'Support-hand frame must have unit scale')
    return value


def load_grips(path):
    path = Path(path).resolve()
    require(path.is_file(), 'Complete weapon grip contract is missing: ' + str(path))
    data = json.loads(path.read_text(encoding='utf-8-sig'), object_pairs_hook=unique_pairs)
    require(type(data) is dict and set(data) == {'schema', 'coordinates', 'source', 'bindings'} and
            type(data['schema']) is int and data['schema'] == 1 and
            data['coordinates'] == 'unreal-local-centimeters-xyzw', 'Unsupported weapon grip schema/coordinates')
    source = data['source']
    require(type(source) is dict and set(source) == {'path', 'sha256'}, 'Missing source grip provenance')
    require(type(source['path']) is str and source['path'] and not Path(source['path']).is_absolute(), 'Source path must be relative')
    artifact = (path.parent / source['path']).resolve()
    require(artifact.is_relative_to(path.parent) and artifact != path and artifact.is_file(), 'Source grip artifact unavailable or outside contract directory')
    require(type(source['sha256']) is str and re.fullmatch('[a-f0-9]{64}', source['sha256']) and
            hashlib.sha256(artifact.read_bytes()).hexdigest() == source['sha256'], 'Source grip artifact hash mismatch')
    require(type(data['bindings']) is list and 0 < len(data['bindings']) <= 256, 'No complete weapon grip bindings')
    rows = {}
    for row in data['bindings']:
        require(type(row) is dict and set(row) == FIELDS, 'Incomplete or unknown grip binding fields')
        for key in ('item_id', 'main_bone', 'support_bone', 'socket'):
            require(type(row[key]) is str and NAME.fullmatch(row[key]), 'Invalid grip name: ' + key)
        require(row['main_bone'].casefold() != row['support_bone'].casefold(), 'Hands must have different bones')
        for key in ('equipment_asset', 'target_mesh', 'weapon_mesh'):
            require(type(row[key]) is str and PACKAGE.fullmatch(row[key]), 'Invalid grip asset path: ' + key)
        require(row['equipment_asset'].startswith('/Game/'), 'Equipment output must be a project asset')
        require(row['target_mesh'].startswith('/Game/'), 'Target must have an explicit project mesh binding')
        identity = row['equipment_asset'].casefold()
        require(identity not in rows, 'Duplicate grip output asset')
        transform(row['weapon_to_socket'])
        transform(row['support_hand_to_weapon'], rigid=True)
        rows[identity] = row
    return rows, source['sha256']


def prepare_grips(ue, root, required_items, output_folder='/Game/AetherCore/Data'):
    """Read and validate the complete batch before callers create or alter any assets.

    Imported body/weapon meshes must already exist. This author step does not infer import axes.
    """
    required_items = list(required_items)
    if not required_items:
        return {}
    rows, source_hash = load_grips(Path(root) / 'ContentSource/Equipment/WeaponGrips.json')
    prepared = {}
    for item_id in required_items:
        output = output_folder + '/DA_' + item_id
        key = output.casefold()
        require(key in rows, 'Missing full grip for ' + item_id)
        row = rows[key]
        require(row['item_id'] == item_id and row['equipment_asset'] == output,
                'Author output does not match explicit grip identity: ' + item_id)
        target = ue.EditorAssetLibrary.load_asset(row['target_mesh'])
        weapon = ue.EditorAssetLibrary.load_asset(row['weapon_mesh'])
        require(isinstance(target, ue.SkeletalMesh) and isinstance(weapon, ue.StaticMesh),
                'Import declared body and weapon before grip authoring: ' + item_id)
        for key, asset in [('target_mesh', target), ('weapon_mesh', weapon)]:
            expected = row[key] + '.' + row[key].rsplit('/', 1)[1]
            require(asset.get_path_name().casefold() == expected.casefold(), 'Redirected grip asset identity: ' + row[key])
        require(ue.AetherEquipmentDefinition.validate_grip_target(target, row['socket'], row['main_bone'], row['support_bone']),
                'Grip bones/socket/reference scale are invalid: ' + item_id)
        if ue.EditorAssetLibrary.does_asset_exist(output):
            require(isinstance(ue.EditorAssetLibrary.load_asset(output), ue.AetherEquipmentDefinition),
                    'Equipment output class conflict: ' + output)
        prepared[key] = (row, target, weapon, source_hash)
    return prepared


def apply_grip(ue, definition, prepared):
    item_id = str(definition.get_editor_property('item_id'))
    output = definition.get_path_name().split('.')[0]
    row, target, weapon, source_hash = prepared[output.casefold()]
    require(output == row['equipment_asset'] and item_id == row['item_id'], 'Grip output identity changed after preflight')
    def native(value):
        q = ue.Quat(*value['rotation_xyzw'])
        return ue.Transform(location=ue.Vector(*value['translation_cm']), rotation=q.rotator(), scale=ue.Vector(*value['scale']))
    for key, value in {'mesh': weapon, 'socket': row['socket'], 'grip_target_mesh': target,
                       'grip_main_hand_bone': row['main_bone'], 'grip_support_hand_bone': row['support_bone'],
                       'grip_source_sha256': source_hash, 'secondary_socket': '',
                       'grip_transform': native(row['weapon_to_socket']),
                       'support_hand_transform': native(row['support_hand_to_weapon']),
                       'support_hand_transform_configured': True}.items():
        definition.set_editor_property(key, value)
