"""Offline text/hash checks only. No Blender, Unreal, network, imports or asset writes."""
import argparse
import ast
import hashlib
import json
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path):
    return json.loads(path.read_text())


def walk(value):
    if isinstance(value, dict):
        for key, item in value.items():
            if key.startswith('UE_') and any(s in key.lower() for s in ['mesh', 'asset', 'axis', 'axes']):
                assert item is None, ('UE target must remain null', key)
            if key in ['rotation_xyzw']:
                assert len(item) == 4 and abs(sum(v*v for v in item)-1) < 1e-4
            if key == 'matrix4x4':
                assert len(item) == 4 and all(len(row) == 4 for row in item)
                assert max(abs(a-b) for a, b in zip(item[3], [0, 0, 0, 1])) < 1e-5
            if key == 'finger_pose_quaternion_xyzw':
                for q in item.values():
                    assert len(q) == 4 and abs(sum(v*v for v in q)-1) < 1e-4
            walk(item)
    elif isinstance(value, list):
        for item in value:
            walk(item)
    elif isinstance(value, float):
        assert math.isfinite(value)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--asset-root', type=Path)
    args = parser.parse_args()
    manifest = read(ROOT/'manifest.json')
    assert manifest['UE_acceptance_status'] == 'CANCELED_BY_USER'
    assert manifest['UE_acceptance_is_todo'] is False
    assert len(manifest['scenes']) == 6
    assert sum(len(item['weapons']) for item in manifest['scenes']) == 7
    count = 0
    for path in ROOT.rglob('*.json'):
        walk(read(path))
        count += 1
    for path in ROOT.rglob('*.py'):
        ast.parse(path.read_text(), filename=str(path))
    for item in manifest['scenes']:
        assert (ROOT/item['contract']).is_file()
        if args.asset_root:
            assert hashlib.sha256((args.asset_root/item['derived_scene']).read_bytes()).hexdigest() == item['sha256']
    for size in [28, 32]:
        q = read(ROOT/f'contracts/Grip{size}.json')
        assert q['quaternion_conversion']['input_order'] == 'WXYZ'
        assert q['quaternion_conversion']['output_order'] == 'XYZW'
        for value in q['finger_pose_quaternion_xyzw'].values():
            wxyz = value[3:] + value[:3]
            assert wxyz[1:] + wxyz[:1] == value
    bow = read(ROOT/'contracts/DynamicBowV3.json')
    assert bow['baseline_contract_values']['support_hand_fixed_transform'] is None
    assert read(ROOT/'evidence/DynamicBowV3Review.json')['whole_garment_clearance'] is False
    index = ROOT/'FileIndex.json'
    if index.exists():
        repository_root = ROOT.parents[3]
        entries = read(index)['files']
        for entry in entries:
            path = repository_root/entry['path']
            assert path.stat().st_size == entry['bytes'], entry['path']
            assert hashlib.sha256(path.read_bytes()).hexdigest() == entry['sha256'], entry['path']
    print(json.dumps({'json_files_checked': count, 'static_checks': 'passed',
                      'scene_hashes_checked': bool(args.asset_root),
                      'bow_library_status': manifest['dynamic_bow_library_status'],
                      'UE_compile_rule_acceptance_tests': 'not_run_canceled'}, indent=2))


if __name__ == '__main__':
    main()
