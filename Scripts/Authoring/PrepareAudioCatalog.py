"""Immutable UE audio authoring, in TWO separate editor processes.

1. --mode build --receipt <absolute receipt.json>
2. --mode verify-publish --receipt <same receipt.json>
Use UnrealEditor-Cmd -run=pythonscript -script="...PrepareAudioCatalog.py ...".
Build never changes the active CatalogId. Verify-publish cold-loads saved packages,
compares them to the source, then atomically switches the config entry.
"""
from pathlib import Path
import argparse
import hashlib
import json
import math
import os
import re
import tempfile
import uuid
import wave

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "ContentSource" / "Audio"
GENERATED_ROOT = "/Game/AetherAudio/Generated"


def source_digest(data):
    return hashlib.sha256(json.dumps(data, sort_keys=True, separators=(",", ":"), allow_nan=False).encode()).hexdigest()


def validate(source=SOURCE):
    data = json.loads((source / "catalog.json").read_text(encoding="utf-8"))
    if data.get("schema_version") != 1:
        raise ValueError("Unsupported audio schema")
    sources = data["sources"]
    seen_sources, seen_paths = set(), set()
    for key, item in sources.items():
        # Canonical lowercase asset keys; UE object/package identities ignore case.
        if not re.fullmatch(r"[a-z][a-z0-9_]*", key) or key.casefold() in seen_sources:
            raise ValueError("Invalid/colliding source ID: " + key)
        seen_sources.add(key.casefold())
        relative = item["file"]
        identity = relative.replace("\\", "/").casefold()
        if identity in seen_paths or "\\" in relative:
            raise ValueError("Case-insensitive source path collision: " + relative)
        seen_paths.add(identity)
        path = (source / relative).resolve()
        if not path.is_relative_to(source.resolve()) or path.suffix.lower() != ".wav":
            raise ValueError("Audio source escapes source directory or is not WAV")
        if hashlib.sha256(path.read_bytes()).hexdigest() != item["sha256"]:
            raise ValueError("Source hash mismatch: " + key)
        original = (source / item["original_file"]).resolve()
        if not original.is_relative_to(source.resolve()) or hashlib.sha256(original.read_bytes()).hexdigest() != item["original_sha256"]:
            raise ValueError("Original provenance hash mismatch: " + key)
        if item["license"] != "CC0-1.0" or not item["author"] or not item["source_page"].startswith("https://"):
            raise ValueError("Missing provenance: " + key)
        with wave.open(str(path), "rb") as wav:
            if wav.getnchannels() != 1 or wav.getsampwidth() != 2 or wav.getframerate() != 48000 or wav.getnframes() <= 0:
                raise ValueError("Expected nonempty mono 48 kHz PCM16: " + key)
    seen = set()
    for event in data["events"]:
        event_id = event["event_id"]
        if not re.fullmatch(r"Aether\.Audio\.[A-Za-z0-9_.]+", event_id, re.IGNORECASE) or event_id.casefold() in seen:
            raise ValueError("Invalid/colliding event ID: " + event_id)
        seen.add(event_id.casefold())
        for key in ("gain", "cooldown_seconds", "inner_radius_cm", "falloff_distance_cm"):
            if type(event[key]) not in (float, int) or not math.isfinite(event[key]):
                raise ValueError("Nonfinite numeric field: " + key)
        if event["gain"] <= 0 or event["cooldown_seconds"] < 0 or event["inner_radius_cm"] < 0 or event["falloff_distance_cm"] <= 0:
            raise ValueError("Out-of-range audio mix field")
        if type(event["max_concurrent"]) is not int or not 1 <= event["max_concurrent"] <= 256:
            raise ValueError("Invalid concurrency budget")
        variants = event["variants"]
        blocked = event.get("unavailable_reason", "")
        if not isinstance(blocked, str):
            raise ValueError("Unavailable reason must be text")
        if blocked:
            if variants:
                raise ValueError("Disabled event must have no playable variants: " + event_id)
            continue
        if not variants or len({value.casefold() for value in variants}) != len(variants):
            raise ValueError("Event needs unique, nonempty real variants: " + event_id)
        for variant in variants:
            if variant not in sources:
                raise ValueError("Missing/case-noncanonical source for " + variant)
    if not seen:
        raise ValueError("Catalog has no events")
    return data


def generated_payload(data, generation):
    events = []
    for item in data["events"]:
        event = dict(item)
        event["event_id"] = item["event_id"].casefold()
        event["unavailable_reason"] = item.get("unavailable_reason", "")
        event["variants"] = [f"{generation}/Waves/{key}.{key}" for key in item["variants"]]
        events.append(event)
    return {"source_digest": source_digest(data), "events": events}


def build(receipt):
    data = validate()
    import unreal as ue
    library = ue.EditorAssetLibrary
    tools = ue.AssetToolsHelpers.get_asset_tools()
    # Fresh immutable generation on EVERY build, including retries after partial failure.
    name = "g_" + source_digest(data)[:16] + "_" + uuid.uuid4().hex
    generation = GENERATED_ROOT + "/" + name
    if library.does_directory_exist(generation):
        raise RuntimeError("Generation collision; refusing to overwrite assets")
    referenced = {key for item in data["events"] for key in item["variants"]}
    for key in sorted(referenced):
        source = data["sources"][key]
        task = ue.AssetImportTask()
        for field, value in dict(filename=str(SOURCE / source["file"]), destination_path=generation + "/Waves",
                                 destination_name=key, automated=True, replace_existing=False,
                                 save=False, factory=ue.SoundFactory()).items():
            task.set_editor_property(field, value)
        tools.import_asset_tasks([task])
        sound = library.load_asset(generation + "/Waves/" + key)
        if not isinstance(sound, ue.SoundWave) or not task.get_editor_property("imported_object_paths"):
            raise RuntimeError("SoundWave import failed; active generation unchanged: " + key)
        for field, value in dict(looping=False, volume=1.0, pitch=1.0,
                                 override_concurrency=False, concurrency_set=[]).items():
            sound.set_editor_property(field, value)
        if not library.save_loaded_asset(sound):
            raise RuntimeError("SoundWave save failed; active generation unchanged: " + key)
    factory = ue.DataAssetFactory()
    factory.set_editor_property("data_asset_class", ue.AetherAudioCatalog)
    catalog = tools.create_asset(name, generation, ue.AetherAudioCatalog, factory)
    if not catalog or not ue.AetherAudioAuthoring.import_generated_catalog(catalog, json.dumps(generated_payload(data, generation), allow_nan=False)):
        raise RuntimeError("Native catalog authoring rejected; active generation unchanged")
    if not library.save_loaded_asset(catalog):
        raise RuntimeError("Catalog save failed; active generation unchanged")
    receipt.parent.mkdir(parents=True, exist_ok=True)
    receipt.write_text(json.dumps(dict(schema_version=1, generation=generation, catalog_path=generation + "/" + name,
                                      source_digest=source_digest(data), build_pid=os.getpid()), indent=2))
    ue.log("Audio generation staged only. Exit this process, then run verify-publish in a NEW editor process.")


def verify_catalog(catalog, payload):
    import unreal as ue
    if str(catalog.get_editor_property("source_digest")) != payload["source_digest"]:
        raise RuntimeError("Saved source digest mismatch")
    events = catalog.get_editor_property("events")
    if len(events) != len(payload["events"]):
        raise RuntimeError("Saved event count mismatch")
    for actual, expected in zip(events, payload["events"]):
        if str(actual.get_editor_property("event_id")).casefold() != expected["event_id"]:
            raise RuntimeError("Saved event identity/order mismatch")
        for field in ("gain", "cooldown_seconds", "inner_radius_cm", "falloff_distance_cm"):
            if not math.isclose(actual.get_editor_property(field), expected[field], rel_tol=1e-6, abs_tol=1e-6):
                raise RuntimeError("Saved mix mismatch: " + field)
        for field in ("max_concurrent", "unavailable_reason"):
            if actual.get_editor_property(field) != expected[field]:
                raise RuntimeError("Saved field mismatch: " + field)
        # Native exporter reads soft paths without relying on Python soft-pointer coercion.
    saved = json.loads(ue.AetherAudioAuthoring.export_generated_catalog(catalog))
    if len(saved["events"]) != len(payload["events"]):
        raise RuntimeError("Native saved event count mismatch")
    for actual, expected in zip(saved["events"], payload["events"]):
        if [value.casefold() for value in actual["variants"]] != [value.casefold() for value in expected["variants"]]:
            raise RuntimeError("Saved variant identity/order mismatch")


def updated_config(text, catalog_id):
    lines = text.splitlines(keepends=True)
    in_section, found = False, 0
    for i, line in enumerate(lines):
        stripped = line.strip()
        if stripped.startswith("["):
            in_section = stripped.casefold() == "[/script/aetheraudio.aetheraudiosettings]"
        elif in_section and re.match(r"CatalogId\s*=", stripped, re.IGNORECASE):
            found += 1
            lines[i] = "CatalogId=" + catalog_id + "\n"
    if found != 1:
        raise ValueError("Expected exactly one audio CatalogId config entry; refusing publication")
    result = "".join(lines)
    # Select only the published generation for cook; preserve old packages on disk.
    start = "; BEGIN AETHER AUDIO ACTIVE COOK RULE"
    end = "; END AETHER AUDIO ACTIVE COOK RULE"
    if result.count(start) != 1 or result.count(end) != 1:
        raise ValueError("Missing/duplicate managed audio cook-rule block")
    begin, finish = result.index(start), result.index(end)
    if finish < begin:
        raise ValueError("Malformed managed audio cook-rule block")
    section = result[:begin].rsplit("[", 1)[-1].split("]", 1)[0]
    if section.casefold() != "/script/engine.assetmanagersettings":
        raise ValueError("Audio cook-rule block is outside AssetManager settings")
    rule = '+PrimaryAssetRules=(PrimaryAssetId="' + catalog_id + '",Rules=(Priority=1,ChunkId=-1,bApplyRecursively=True,CookRule=AlwaysCook))'
    return result[:begin] + start + "\n" + rule + "\n" + result[finish:]


def publish_config(catalog_id):
    config = ROOT / "Config/DefaultGame.ini"
    lock = config.with_suffix(".audio-publish.lock")
    fd = os.open(lock, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
    temporary = None
    try:
        os.close(fd)
        previous = config.read_bytes()
        replacement = updated_config(previous.decode("utf-8"), catalog_id).encode("utf-8")
        backup = ROOT / "Saved/AudioAuthoring" / ("DefaultGame.before-" + uuid.uuid4().hex + ".ini")
        backup.parent.mkdir(parents=True, exist_ok=True)
        backup.write_bytes(previous)
        with tempfile.NamedTemporaryFile(dir=config.parent, prefix=".audio-", delete=False) as stream:
            temporary = Path(stream.name)
            stream.write(replacement)
            stream.flush()
            os.fsync(stream.fileno())
        if config.read_bytes() != previous:
            raise RuntimeError("Config changed during publication; active selection not replaced")
        os.replace(temporary, config)
        temporary = None
    finally:
        if temporary is not None: temporary.unlink(missing_ok=True)
        lock.unlink(missing_ok=True)


def verify_publish(receipt):
    record = json.loads(receipt.read_text())
    if record["build_pid"] == os.getpid():
        raise RuntimeError("Verification must run in a different editor process after the build process exits")
    data = validate()
    digest = source_digest(data)
    if record["source_digest"] != digest:
        raise RuntimeError("Source changed after build; build a fresh generation")
    generation = record["generation"]
    if not re.fullmatch(re.escape(GENERATED_ROOT) + r"/g_[a-f0-9]{16}_[a-f0-9]{32}", generation):
        raise ValueError("Invalid generated path")
    name = generation.rsplit("/", 1)[1]
    if record["catalog_path"] != generation + "/" + name:
        raise ValueError("Invalid catalog path")
    import unreal as ue
    library = ue.EditorAssetLibrary
    catalog = library.load_asset(record["catalog_path"])
    if not isinstance(catalog, ue.AetherAudioCatalog):
        raise RuntimeError("Saved catalog missing or wrong type")
    verify_catalog(catalog, generated_payload(data, generation))
    if not ue.AetherAudioAuthoring.is_catalog_registered(catalog):
        raise RuntimeError("Configured AssetManager scan did not register saved catalog; selection not published")
    referenced = {key for event in data["events"] for key in event["variants"]}
    for key in referenced:
        sound = library.load_asset(generation + "/Waves/" + key)
        if not isinstance(sound, ue.SoundWave):
            raise RuntimeError("Saved SoundWave missing: " + key)
        if sound.get_editor_property("looping") or sound.get_editor_property("num_channels") != 1:
            raise RuntimeError("Saved wave format mismatch: " + key)
        for field in ("volume", "pitch"):
            if not math.isclose(sound.get_editor_property(field), 1.0):
                raise RuntimeError("Saved wave scalar mismatch: " + key)
        if sound.get_editor_property("override_concurrency") or sound.get_editor_property("concurrency_set"):
            raise RuntimeError("Saved wave concurrency mismatch: " + key)
        with wave.open(str(SOURCE / data["sources"][key]["file"]), "rb") as wav:
            duration = wav.getnframes() / wav.getframerate()
        if not math.isclose(sound.get_editor_property("duration"), duration, abs_tol=0.002):
            raise RuntimeError("Saved wave duration mismatch: " + key)
    publish_config("AetherAudioCatalog:" + name)
    ue.log("Saved catalog verified in a second process and config selection published. Restart UE to activate. Playback/listening/cook NOT verified.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--mode", required=True, choices=("build", "verify-publish"))
    parser.add_argument("--receipt", required=True, type=Path)
    args = parser.parse_args()
    (build if args.mode == "build" else verify_publish)(args.receipt.resolve())
