"""Offline authoring contract tests. Run only in the unified verification phase."""
from pathlib import Path
import copy
import importlib.util
import json
import shutil
import tempfile
import types
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("audio_author", ROOT / "Scripts/Authoring/PrepareAudioCatalog.py")
author = importlib.util.module_from_spec(spec)
spec.loader.exec_module(author)


class AudioCatalogTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.source = Path(self.temp.name) / "Audio"
        shutil.copytree(author.SOURCE, self.source)
        self.data = json.loads((self.source / "catalog.json").read_text())

    def tearDown(self):
        self.temp.cleanup()

    def validate(self):
        (self.source / "catalog.json").write_text(json.dumps(self.data))
        return author.validate(self.source)

    def test_complete_reaction_catalog(self):
        data = self.validate()
        self.assertEqual({item["event_id"] for item in data["events"]}, {
            "Aether.Audio.Reaction.Break", "Aether.Audio.Reaction.Freeze", "Aether.Audio.Reaction.Extinguish"})

    def test_missing_variant_is_not_success(self):
        self.data["events"][0]["variants"] = []
        with self.assertRaises(ValueError): self.validate()

    def test_duplicate_event_rejected(self):
        self.data["events"].append(self.data["events"][0])
        with self.assertRaises(ValueError): self.validate()

    def test_nonfinite_gain_rejected(self):
        self.data["events"][0]["gain"] = float("nan")
        with self.assertRaises(ValueError): self.validate()

    def test_invalid_budget_rejected(self):
        self.data["events"][0]["max_concurrent"] = 0
        with self.assertRaises(ValueError): self.validate()

    def test_unresolved_source_rejected(self):
        self.data["events"][0]["variants"] = ["does_not_exist"]
        with self.assertRaises(ValueError): self.validate()

    def test_corrupt_source_rejected(self):
        next(iter(self.data["sources"].values()))["sha256"] = "0" * 64
        with self.assertRaises(ValueError): self.validate()

    def test_path_traversal_rejected(self):
        next(iter(self.data["sources"].values()))["file"] = "../outside.wav"
        with self.assertRaises(ValueError): self.validate()

    def test_disabled_event_has_no_playable_variants(self):
        self.data["events"][0]["unavailable_reason"] = "Awaiting corrected recording"
        self.data["events"][0]["variants"] = []
        data = self.validate()
        self.assertEqual(data["events"][0]["variants"], [])

    def test_disabled_event_with_variants_rejected(self):
        self.data["events"][0]["unavailable_reason"] = "Not a matching recording"
        with self.assertRaises(ValueError): self.validate()

    def test_case_colliding_events_rejected(self):
        duplicate = copy.deepcopy(self.data["events"][0])
        duplicate["event_id"] = duplicate["event_id"].swapcase()
        self.data["events"].append(duplicate)
        with self.assertRaises(ValueError): self.validate()

    def test_noncanonical_source_case_rejected(self):
        key = next(iter(self.data["sources"]))
        self.data["sources"][key.upper()] = copy.deepcopy(self.data["sources"][key])
        with self.assertRaises(ValueError): self.validate()

    def test_case_colliding_source_paths_rejected(self):
        value = copy.deepcopy(next(iter(self.data["sources"].values())))
        value["file"] = value["file"].swapcase()
        self.data["sources"]["new_key"] = value
        with self.assertRaises(ValueError): self.validate()

    def test_case_colliding_variants_rejected(self):
        event = self.data["events"][0]
        event["variants"].append(event["variants"][0].upper())
        with self.assertRaises(ValueError): self.validate()

    def test_generation_payload_preserves_variant_order(self):
        payload = author.generated_payload(self.data, "/Game/AetherAudio/Generated/g_example")
        self.assertEqual(payload["events"][0]["variants"], [
            "/Game/AetherAudio/Generated/g_example/Waves/" + key + "." + key
            for key in self.data["events"][0]["variants"]])

    def test_saved_mix_mismatch_prevents_verification(self):
        payload = author.generated_payload(self.data, "/Game/AetherAudio/Generated/g_example")
        actual = copy.deepcopy(payload)
        actual["events"][0]["gain"] += 0.25
        class Reflected:
            def __init__(self, values): self.values = values
            def get_editor_property(self, field): return self.values[field]
        catalog = Reflected(dict(source_digest=actual["source_digest"], events=[Reflected(row) for row in actual["events"]]))
        fake = types.SimpleNamespace(AetherAudioAuthoring=types.SimpleNamespace(export_generated_catalog=lambda obj: json.dumps(actual)))
        with mock.patch.dict("sys.modules", unreal=fake):
            with self.assertRaises(RuntimeError): author.verify_catalog(catalog, payload)

    def test_saved_variant_order_mismatch_prevents_verification(self):
        payload = author.generated_payload(self.data, "/Game/AetherAudio/Generated/g_example")
        actual = copy.deepcopy(payload)
        actual["events"][0]["variants"].reverse()
        class Reflected:
            def __init__(self, values): self.values = values
            def get_editor_property(self, field): return self.values[field]
        catalog = Reflected(dict(source_digest=actual["source_digest"], events=[Reflected(row) for row in actual["events"]]))
        fake = types.SimpleNamespace(AetherAudioAuthoring=types.SimpleNamespace(export_generated_catalog=lambda obj: json.dumps(actual)))
        with mock.patch.dict("sys.modules", unreal=fake):
            with self.assertRaises(RuntimeError): author.verify_catalog(catalog, payload)

    def test_publication_changes_selection_and_cook_rule_together(self):
        before = (ROOT / "Config/DefaultGame.ini").read_text()
        after = author.updated_config(before, "AetherAudioCatalog:g_example")
        self.assertIn("CatalogId=AetherAudioCatalog:g_example", after)
        self.assertIn('PrimaryAssetId="AetherAudioCatalog:g_example"', after)
        self.assertEqual(after.count("BEGIN AETHER AUDIO ACTIVE COOK RULE"), 1)
        self.assertIn("CookRule=NeverCook", after)

    def test_publication_failure_keeps_previous_config(self):
        root = Path(self.temp.name) / "project"
        config = root / "Config/DefaultGame.ini"
        config.parent.mkdir(parents=True)
        before = (ROOT / "Config/DefaultGame.ini").read_bytes()
        config.write_bytes(before)
        with mock.patch.object(author, "ROOT", root), mock.patch.object(author.os, "replace", side_effect=OSError("injected failure")):
            with self.assertRaises(OSError): author.publish_config("AetherAudioCatalog:g_example")
        self.assertEqual(config.read_bytes(), before)
        backups = list((root / "Saved/AudioAuthoring").glob("DefaultGame.before-*.ini"))
        self.assertEqual(len(backups), 1)
        self.assertEqual(backups[0].read_bytes(), before)

    def test_missing_cook_block_prevents_publication(self):
        with self.assertRaises(ValueError):
            author.updated_config("[/Script/AetherAudio.AetherAudioSettings]\nCatalogId=old\n", "AetherAudioCatalog:g_example")


if __name__ == "__main__":
    unittest.main()
