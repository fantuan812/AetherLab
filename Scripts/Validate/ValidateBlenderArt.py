"""Validate checked-in Blender staging files, not Unreal runtime integration."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--scene', default='SCN_01', choices=['SCN_01'])
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[2]
    root = repo / 'Art' / 'AetherLab' / 'Scenes' / args.scene
    manifest = json.loads((root / 'docs/checksums.json').read_text(encoding='utf-8'))
    errors = []
    for entry in manifest:
        relative = Path(entry['file'])
        if relative.is_absolute() or '..' in relative.parts:
            errors.append(f'Unsafe manifest path: {relative}')
            continue
        path = root / relative
        if not path.is_file():
            errors.append(f'Missing: {relative}')
            continue
        data = path.read_bytes()
        if len(data) != entry['bytes']:
            errors.append(f'Size mismatch: {relative}')
        if hashlib.sha256(data).hexdigest() != entry['sha256']:
            errors.append(f'SHA256 mismatch: {relative}')
        if len(data) >= 20 * 1024 * 1024:
            errors.append(f'Asset exceeds this batch 20 MiB budget: {relative}')
        if path.suffix == '.blend' and not data.startswith(b'BLENDER'):
            errors.append(f'Invalid Blender header: {relative}')
        if path.suffix == '.fbx' and not data.startswith(b'Kaydara FBX Binary'):
            errors.append(f'Invalid binary FBX header: {relative}')
        if path.suffix == '.glb' and not data.startswith(b'glTF'):
            errors.append(f'Invalid GLB header: {relative}')
        if path.suffix == '.py' and b'/workspace/scratch/' in data:
            errors.append(f'Nonportable authoring path: {relative}')
    fbx = list((root / 'exports/fbx').glob('*.fbx'))
    if len(fbx) != 10:
        errors.append(f'Expected 10 FBX modules, found {len(fbx)}')
    expected = {'SM_BarrelWater.fbx', 'SM_ShelteredBrazierFire.fbx', 'SM_BrokenCart.fbx'}
    if not expected.issubset({p.name for p in fbx}):
        errors.append('Required separately editable reaction exports absent')
    for item in errors:
        print('FAIL:', item)
    if errors:
        return 1
    print(f'PASS: {args.scene}; {len(manifest)} files verified; 10 FBX modules')
    print('Blender evidence retained. Unreal import/runtime/Cook: NOT RUN.')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
