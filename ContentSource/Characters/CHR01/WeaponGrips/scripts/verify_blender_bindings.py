"""Read-only finite Blender measurements; no import/export, rendering or scene save.

blender -b --python verify_blender_bindings.py -- --asset-root /unpacked --mode Staff
Asset root contains source/*.blend from the separately delivered Library packages.
Prints JSON between CHR01_RESULT_BEGIN/END. It does not overwrite evidence reports.
"""
import argparse
import hashlib
import json
import math
import sys
from pathlib import Path

import bpy
import numpy as np
from mathutils import Matrix, Vector

ROOT = Path(__file__).resolve().parents[1]


def load(path):
    return json.loads(path.read_text())


def file_sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def rest_sha(rig):
    data = {b.name: {'parent': b.parent.name if b.parent else None,
                    'matrix_local': [list(row) for row in b.matrix_local],
                    'head': list(b.head_local), 'tail': list(b.tail_local)}
            for b in rig.data.bones}
    return hashlib.sha256(json.dumps(data, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def rotation_angle(matrix):
    # Match the archived finite audit: orthogonal polar factor from SVD.
    u, _, vt = np.linalg.svd(np.asarray(matrix.to_3x3(), dtype=np.float64))
    r = u @ vt
    sine = np.linalg.norm([r[2, 1]-r[1, 2], r[0, 2]-r[2, 0], r[1, 0]-r[0, 1]]) * .5
    return math.degrees(math.atan2(sine, (np.trace(r)-1)*.5))


def frame(value):
    bpy.context.scene.frame_set(int(value), subframe=value-int(value))
    bpy.context.view_layer.update()


def fixed_measurements(contract, rig):
    maxima = {}
    for i in range(193):
        frame(1+i*.5)
        for binding in contract['source_bindings']:
            weapon = bpy.data.objects[binding['root_object']].matrix_world
            hand = rig.matrix_world @ rig.pose.bones[binding['main_bone']].matrix
            pairs = [(binding['weapon']+'_main', hand.inverted() @ weapon,
                      Matrix(binding['weapon_local_to_hand_local']['matrix4x4']))]
            if binding['support_bone']:
                support = rig.matrix_world @ rig.pose.bones[binding['support_bone']].matrix
                pairs.append((binding['weapon']+'_support', weapon.inverted() @ support,
                              Matrix(binding['support_hand_local_to_weapon_local']['matrix4x4'])))
            for name, actual, expected in pairs:
                delta = expected.inverted() @ actual
                values = {'translation_error_m': delta.translation.length,
                          'rotation_error_deg': rotation_angle(delta)}
                maxima.setdefault(name, dict(values))
                for key, value in values.items():
                    maxima[name][key] = max(maxima[name][key], value)
    return {'sample_count': 193, 'frame_step': .5, 'maxima': maxima,
            'collision_tested': False}


def bow_measurements(contract, rig):
    values = contract['baseline_contract_values']
    contact = Matrix(values['anatomy']['l']['contact_frame_hand_local'])
    bow = bpy.data.objects['WPN_06_Rainvalley_Recurve_Bow']
    special = [1, 25, 26.5, 29, 32, 37, 49, 61, 61.125, 61.5, 62,
               62.5625, 62.625, 62.6875, 62.75, 62.8125, 63, 63.125,
               65, 69, 69.125, 73, 81, 97]
    frames = sorted(set([1+i*.5 for i in range(193)] + [61+i*.125 for i in range(65)] + special))
    maximum = {'main_contact_origin_error_m': 0., 'string_to_helper_endpoint_error_m': 0.,
               'actual_limb_ring_center_offset_m': 0.}
    for value in frames:
        frame(value)
        dg = bpy.context.evaluated_depsgraph_get()
        hand = rig.matrix_world @ rig.pose.bones['hand_l'].matrix
        maximum['main_contact_origin_error_m'] = max(maximum['main_contact_origin_error_m'],
                                                   ((hand @ contact).translation-bow.matrix_world.translation).length)
        nock = bpy.data.objects['Bow_StringNock_Target'].matrix_world.translation
        for side in ['upper', 'lower']:
            string = bpy.data.objects['Bow_String_'+side]
            start = string.matrix_world @ Vector((0, 0, 0))
            end = string.matrix_world @ Vector((0, 0, 1))
            tip = bpy.data.objects['Bow_StringTip_Target_'+side].matrix_world.translation
            maximum['string_to_helper_endpoint_error_m'] = max(maximum['string_to_helper_endpoint_error_m'],
                                                               (start-nock).length, (end-tip).length)
            limb = bpy.data.objects['Bow_'+side+'_LaminatedWood']
            evaluated = limb.evaluated_get(dg)
            mesh = evaluated.to_mesh()
            try:
                # The identified authored limb uses 49 cross-section rings.
                if len(mesh.vertices) % 49:
                    raise ValueError('Unexpected limb topology; do not infer ring vertices')
                count = len(mesh.vertices)//49
                center = sum((limb.matrix_world @ v.co for v in mesh.vertices[-count:]), Vector()) / count
                maximum['actual_limb_ring_center_offset_m'] = max(maximum['actual_limb_ring_center_offset_m'],
                                                                (center-end).length)
            finally:
                evaluated.to_mesh_clear()
    return {'sample_count': len(frames), 'maxima': maximum,
            'collision_tested': False, 'draw_contact_recomputed': False,
            'scope': 'Main contact origin and two distinct endpoint metrics only; not full author/independent collision audits.'}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--asset-root', type=Path, required=True)
    parser.add_argument('--mode', required=True, choices=['SwordShield', 'Staff', 'Hammer', 'Spear', 'Greatsword', 'DynamicBow'])
    args = parser.parse_args(sys.argv[sys.argv.index('--')+1:])
    manifest = load(ROOT/'manifest.json')
    item = next(x for x in manifest['scenes'] if x['mode'] == args.mode)
    path = args.asset_root/item['derived_scene']
    before = file_sha(path)
    if before != item['sha256']:
        raise ValueError('Scene hash differs from the reviewed exact identity')
    bpy.ops.wm.open_mainfile(filepath=str(path), load_ui=False)
    rig = bpy.data.objects['CHR01_Wanderer_Rig65']
    frozen = load(ROOT/'contracts/FrozenCharacter.json')['frozen_character']
    if len(rig.data.bones) != frozen['bone_count'] or rest_sha(rig) != frozen['rest_sha256']:
        raise ValueError('Bone count/rest identity differs from the frozen source')
    scene = bpy.context.scene
    if scene.unit_settings.scale_length != 1 or [scene.frame_start, scene.frame_end] != [1, 97] or scene.render.fps != 24:
        raise ValueError('Unexpected scene unit/frame/fps contract')
    contract = load(ROOT/item['contract'])
    result = (bow_measurements if args.mode == 'DynamicBow' else fixed_measurements)(contract, rig)
    result.update({'mode': args.mode, 'scene_sha256': before, 'rest_sha256': rest_sha(rig),
                   'scene_file_unchanged': file_sha(path) == before,
                   'blender_version': bpy.app.version_string, 'UE_run': False})
    print('CHR01_RESULT_BEGIN\n'+json.dumps(result, indent=2)+'\nCHR01_RESULT_END')


if __name__ == '__main__':
    main()
