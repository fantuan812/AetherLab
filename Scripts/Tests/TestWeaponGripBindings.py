"""Synthetic contract tests, intentionally not executed during the implementation phase.

Fixtures are created only inside TemporaryDirectory; no test row is production grip data.
"""
import copy
import hashlib
import json
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'Authoring'))
from WeaponGripBindings import load_grips, transform, prepare_grips, apply_grip


class WeaponGripBindingsTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.path = self.root / 'WeaponGrips.json'
        proof = self.root / 'synthetic-test-proof.json'
        proof.write_text('{"synthetic_test_only":true}\n')
        self.rigid = {'translation_cm': [1, 2, 3], 'rotation_xyzw': [0, 0, 0, 1], 'scale': [1, 1, 1]}
        self.row = {'item_id': 'TestWeapon', 'equipment_asset': '/Game/AetherCore/Data/DA_TestWeapon',
                    'target_mesh': '/Game/Tests/Body', 'weapon_mesh': '/Game/Tests/Weapon',
                    'main_bone': 'TestMain', 'support_bone': 'TestSupport', 'socket': 'TestSocket',
                    'weapon_to_socket': copy.deepcopy(self.rigid), 'support_hand_to_weapon': copy.deepcopy(self.rigid)}
        self.data = {'schema': 1, 'coordinates': 'unreal-local-centimeters-xyzw',
                     'source': {'path': proof.name, 'sha256': hashlib.sha256(proof.read_bytes()).hexdigest()},
                     'bindings': [self.row]}

    def tearDown(self):
        self.temporary.cleanup()

    def load(self):
        self.path.write_text(json.dumps(self.data))
        return load_grips(self.path)

    def test_explicit_contract(self):
        rows, source_hash = self.load()
        self.assertEqual(len(rows), 1)
        self.assertEqual(source_hash, self.data['source']['sha256'])
        self.assertEqual(rows[self.row['equipment_asset'].casefold()]['support_hand_to_weapon'], self.rigid)

    def test_missing_contract_has_no_default(self):
        with self.assertRaisesRegex(ValueError, 'missing'):
            load_grips(self.path)

    def test_stale_provenance(self):
        self.data['source']['sha256'] = '0' * 64
        with self.assertRaisesRegex(ValueError, 'hash mismatch'):
            self.load()

    def test_duplicate_asset_case_alias(self):
        other = copy.deepcopy(self.row)
        other['equipment_asset'] = other['equipment_asset'].upper()
        # Canonical prefix spelling remains required; duplicate interior case still aliases.
        other['equipment_asset'] = '/Game/' + other['equipment_asset'].split('/', 2)[2]
        self.data['bindings'].append(other)
        with self.assertRaisesRegex(ValueError, 'Duplicate'):
            self.load()

    def test_position_only_and_unknown_fields_rejected(self):
        del self.row['support_hand_to_weapon']['rotation_xyzw']
        with self.assertRaises(ValueError):
            self.load()
        self.row['support_hand_to_weapon'] = copy.deepcopy(self.rigid)
        self.row['support_hand_offset'] = [0, 0, 24]
        with self.assertRaises(ValueError):
            self.load()

    def test_quaternion_scale_and_finite_contract(self):
        for scale in ([0, 0, 0], [-1, -1, -1], [1, 2, 1]):
            value = copy.deepcopy(self.rigid); value['scale'] = scale
            with self.assertRaises(ValueError): transform(value)
        for q in ([0, 0, 0, 2], [0, 0, float('nan'), 1]):
            value = copy.deepcopy(self.rigid); value['rotation_xyzw'] = q
            with self.assertRaises(ValueError): transform(value)
        value = copy.deepcopy(self.rigid); value['scale'] = [2, 2, 2]
        with self.assertRaises(ValueError): transform(value, rigid=True)
        transform(value)  # An explicit uniform visual scale is distinct from rigid support frame scale.

    def test_wrong_coordinate_system_rejected(self):
        self.data['coordinates'] = 'blender-z-up-meters'
        with self.assertRaises(ValueError):
            self.load()

    def test_duplicate_json_key_rejected(self):
        self.path.write_text('{"schema":1,"schema":1}')
        with self.assertRaisesRegex(ValueError, 'Duplicate'):
            load_grips(self.path)

    def test_preflight_failure_before_any_editor_read_or_mutation(self):
        class EditorMustNotBeCalled:
            def __getattr__(self, name):
                raise AssertionError('Editor access before whole-contract validation: ' + name)
        with self.assertRaisesRegex(ValueError, 'missing'):
            prepare_grips(EditorMustNotBeCalled(), self.root, ['TestWeapon'])

    def test_batch_keys_and_apply_match_exact_output(self):
        # Fake UE objects exercise the successful batch path without importing or running Unreal.
        other = copy.deepcopy(self.row)
        other['item_id'] = 'OtherWeapon'
        other['equipment_asset'] = '/Game/AetherCore/Data/DA_OtherWeapon'
        other['support_hand_to_weapon']['translation_cm'] = [9, 8, 7]
        self.data['bindings'].append(other)
        directory = self.root / 'ContentSource/Equipment'; directory.mkdir(parents=True)
        (directory / 'WeaponGrips.json').write_text(json.dumps(self.data))
        (directory / self.data['source']['path']).write_bytes((self.root / self.data['source']['path']).read_bytes())
        class Asset:
            def __init__(self, package): self.package = package
            def get_path_name(self): return self.package + '.' + self.package.rsplit('/', 1)[1]
        class SkeletalMesh(Asset): pass
        class StaticMesh(Asset): pass
        class Definition(Asset):
            def __init__(self, row):
                super().__init__(row['equipment_asset']); self.values = {'item_id': row['item_id']}
            def get_editor_property(self, key): return self.values[key]
            def set_editor_property(self, key, value): self.values[key] = value
            @staticmethod
            def validate_grip_target(*args): return True
        class Quat:
            def __init__(self, *values): self.values = values
            def rotator(self): return self.values
        outputs = [Definition(self.row), Definition(other)]
        assets = {self.row['target_mesh']: SkeletalMesh(self.row['target_mesh']),
                  self.row['weapon_mesh']: StaticMesh(self.row['weapon_mesh'])}
        assets.update({value.package: value for value in outputs})
        ue = SimpleNamespace(SkeletalMesh=SkeletalMesh, StaticMesh=StaticMesh, AetherEquipmentDefinition=Definition,
                             EditorAssetLibrary=SimpleNamespace(load_asset=assets.get, does_asset_exist=lambda path: path in assets),
                             Quat=Quat, Vector=lambda *values: values, Transform=lambda **values: values)
        prepared = prepare_grips(ue, self.root, ['TestWeapon', 'OtherWeapon'])
        self.assertEqual(set(prepared), {value.package.casefold() for value in outputs})
        self.assertTrue(all(value.values == {'item_id': row['item_id']} for value, row in zip(outputs, self.data['bindings'])))
        for value in outputs: apply_grip(ue, value, prepared)
        self.assertEqual(outputs[0].values['support_hand_transform']['location'], (1, 2, 3))
        self.assertEqual(outputs[1].values['support_hand_transform']['location'], (9, 8, 7))
        self.assertTrue(all(value.values['support_hand_transform_configured'] for value in outputs))


if __name__ == '__main__':
    unittest.main()
