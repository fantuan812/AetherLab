"""UE 编辑器内动作资源制作。输入由 MotionAuthor.py 生成，不使用开发者绝对路径。"""
import pathlib
import unreal as ue

ROOT = pathlib.Path(ue.Paths.project_dir()).resolve()
OUTPUT = "/Game/Animation/Motion"
LIBRARY = ue.EditorAssetLibrary

def require(path):
    value = LIBRARY.load_asset(path)
    if not value:
        raise RuntimeError("缺失动作资源：" + path)
    return value

def main():
    skeleton = ROOT / "ContentSource/Motion/G1Skeleton.json"
    if not skeleton.is_file():
        raise RuntimeError("先用 MotionAuthor.py extract 从锁定模型生成骨架描述")
    source = LIBRARY.load_asset(OUTPUT + "/SK_G1MotionSource")
    if not source:
        source, reason = ue.AetherMotionAuthoring.create_source(str(skeleton))
        if not source:
            raise RuntimeError(reason)
    resources = [source, source.get_editor_property("skeleton")]
    for body in ["Manny", "Quinn"]:
        target = require("/Game/Characters/Mannequins/Meshes/SKM_" + body + "_Simple")
        profile_path = OUTPUT + "/DA_Motion" + body
        if not LIBRARY.does_asset_exist(profile_path):
            ok, reason = ue.AetherMotionAuthoring.create_retarget_assets(source, target, body)
            if not ok:
                raise RuntimeError(reason)
        for path in [profile_path, OUTPUT + "/RTG_G1_" + body, OUTPUT + "/RTG_" + body + "_G1", OUTPUT + "/IK_" + body]:
            resources.append(require(path))
    resources.append(require(OUTPUT + "/IK_G1"))
    for clip in sorted((ROOT / "ContentSource/Motion/Clips").glob("*.json")):
        path = OUTPUT + "/Baked/AN_" + clip.stem
        animation = LIBRARY.load_asset(path)
        if not animation:
            animation, reason = ue.AetherMotionAuthoring.import_clip(str(clip), source, path)
            if not animation:
                raise RuntimeError(reason)
        resources.append(animation)
    path = OUTPUT + "/DA_MotionCook"
    label = LIBRARY.load_asset(path)
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
