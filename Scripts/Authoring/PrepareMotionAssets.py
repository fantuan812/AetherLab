"""UE 编辑器内动作资源制作。输入由 MotionAuthor.py 生成，不使用开发者绝对路径。"""
import pathlib
import json
import unreal as ue

def load_optional(path):
    # 首次创建不存在的目标是正常情况；不要向命令行作者过程记录误导性 Error。
    return ue.EditorAssetLibrary.load_asset(path) if ue.EditorAssetLibrary.does_asset_exist(path) else None


ROOT = pathlib.Path(ue.Paths.project_dir()).resolve()
OUTPUT = "/Game/Animation/Motion"
LIBRARY = ue.EditorAssetLibrary

def require(path):
    value = load_optional(path)
    if not value:
        raise RuntimeError("缺失动作资源：" + path)
    return value

def main():
    skeleton = ROOT / "ContentSource/Motion/G1Skeleton.json"
    if not skeleton.is_file():
        raise RuntimeError("先用 MotionAuthor.py extract 从锁定模型生成骨架描述")
    source = load_optional(OUTPUT + "/SK_G1MotionSource")
    if not source:
        source, reason = ue.AetherMotionAuthoring.create_source(str(skeleton))
        if not source:
            raise RuntimeError(reason)
    resources = [source, source.get_editor_property("skeleton")]
    skeleton_data = json.loads(skeleton.read_text(encoding="utf-8-sig"))
    profiles = []
    boundaries = {}
    for body in ["Manny", "Quinn"]:
        target = require("/Game/Characters/Mannequins/Meshes/SKM_" + body + "_Simple")
        profile_path = OUTPUT + "/DA_Motion" + body
        if not LIBRARY.does_asset_exist(profile_path):
            reason = ue.AetherMotionAuthoring.create_retarget_assets(source, target, body)
            # UE Python 将 bool + 单 out 参数映射为成功时 out 值，失败时 None；空字符串也是成功。
            if reason is None:
                raise RuntimeError("重定向作者失败：" + body)
        profiles.append(require(profile_path))
        for path in [profile_path, OUTPUT + "/RTG_G1_" + body, OUTPUT + "/RTG_" + body + "_G1", OUTPUT + "/IK_" + body]:
            resources.append(require(path))
    resources.append(require(OUTPUT + "/IK_G1"))
    for clip in sorted((ROOT / "ContentSource/Motion/Clips").glob("*.json")):
        path = OUTPUT + "/Baked/AN_" + clip.stem
        animation = load_optional(path)
        if not animation:
            animation, reason = ue.AetherMotionAuthoring.import_clip(str(clip), source, path)
            if not animation:
                raise RuntimeError(reason)
        resources.append(animation)
        data = json.loads(clip.read_text(encoding="utf-8-sig"))
        style_names = {"idle": "Idle", "walk": "Walk", "injured": "Injured", "injured_walk": "Injured",
                       "combat": "Combat", "walk_boxing": "Combat", "strafeleft": "StrafeLeft", "walk_left": "StrafeLeft",
                       "straferight": "StrafeRight", "walk_right": "StrafeRight", "crouch": "Crouch", "crouch_idle": "CrouchIdle"}
        style = style_names.get(clip.stem.lower())
        if style:
            if data.get("skeletonSha256") != skeleton_data["skeletonSha256"] or len(data["roots"]) < 4:
                raise RuntimeError("边界骨架/帧数无效：" + str(clip))
            name = "DA_Boundary_" + style
            asset = load_optional(OUTPUT + "/" + name)
            if not asset:
                factory = ue.DataAssetFactory()
                factory.set_editor_property("data_asset_class", ue.AetherMotionBoundaryAsset)
                asset = ue.AssetToolsHelpers.get_asset_tools().create_asset(name, OUTPUT, ue.AetherMotionBoundaryAsset, factory)
            asset.set_editor_property("skeleton_sha256", data["skeletonSha256"])
            asset.set_editor_property("native_revision", data["nativeRevision"])
            asset.set_editor_property("roots", [v for frame in data["roots"][-4:] for v in frame])
            asset.set_editor_property("rotations", [v for frame in data["rotations"][-4:] for v in frame])
            if not LIBRARY.save_loaded_asset(asset):
                raise RuntimeError("边界资产保存失败")
            boundaries[style] = asset
            resources.append(asset)
    for profile in profiles:
        profile.set_editor_property("skeleton_sha256", skeleton_data["skeletonSha256"])
        styles = dict(profile.get_editor_property("styles"))
        for key, filename in [("Crouch", "crouch"), ("CrouchIdle", "crouch_idle")]:
            if (ROOT / "ContentSource/Motion/Styles" / (filename + ".mbstyle")).is_file():
                styles[ue.Name(key)] = filename
        profile.set_editor_property("styles", styles)
        profile.set_editor_property("transition_boundaries", {k:v for k,v in boundaries.items() if ue.Name(k) in styles})
        if not LIBRARY.save_loaded_asset(profile):
            raise RuntimeError("动作配置保存失败")
    path = OUTPUT + "/DA_MotionCook"
    label = load_optional(path)
    if not label:
        factory = ue.DataAssetFactory()
        factory.set_editor_property("data_asset_class", ue.PrimaryAssetLabel)
        label = ue.AssetToolsHelpers.get_asset_tools().create_asset("DA_MotionCook", OUTPUT, ue.PrimaryAssetLabel, factory)
    rules = ue.PrimaryAssetRules()
    rules.set_editor_property("cook_rule", ue.PrimaryAssetCookRule.ALWAYS_COOK)
    label.set_editor_property("rules", rules)
    label.set_editor_property("explicit_assets", resources)
    label.set_editor_property("is_runtime_label", True)
    LIBRARY.save_loaded_asset(label)
    ue.log("动作资源制作完成；动作质量、骨架方向和双人物性能尚须实际验收。")
if __name__ == "__main__":
    main()
