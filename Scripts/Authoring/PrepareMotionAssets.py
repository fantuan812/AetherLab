"""UE 编辑器内动作资源制作。输入由 MotionAuthor.py 生成，不使用开发者绝对路径。"""
import pathlib
import json
import sys
import argparse
import unreal as ue

def load_optional(path):
    # 首次创建不存在的目标是正常情况；不要向命令行作者过程记录误导性 Error。
    return ue.EditorAssetLibrary.load_asset(path) if ue.EditorAssetLibrary.does_asset_exist(path) else None


ROOT = pathlib.Path(ue.Paths.project_dir()).resolve()
OUTPUT = "/Game/Animation/Motion"
LIBRARY = ue.EditorAssetLibrary
sys.path.insert(0, str(ROOT / "Scripts/Authoring"))
from MotionBindings import load_bindings, configured_binding
BINDINGS = ROOT / "Content/AetherCore/Definitions/MotionBindings.json"

def require(path):
    value = load_optional(path)
    if not value:
        raise RuntimeError("缺失动作资源：" + path)
    return value

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--body', action='append', help='明确骨架 ID；不指定时只制作 configured 条目')
    args = parser.parse_args()
    bindings = load_bindings(BINDINGS)
    selected = [configured_binding(bindings, identity) for identity in args.body] if args.body else [row for row in bindings.values() if row['state'] == 'configured']
    skeleton = ROOT / "ContentSource/Motion/G1Skeleton.json"
    if not skeleton.is_file():
        raise RuntimeError("先用 MotionAuthor.py extract 从锁定模型生成骨架描述")
    # 管理的隐藏源网格可原位重建，保证骨架、蒙皮顶点和新的坐标基一起更新。
    source, reason = ue.AetherMotionAuthoring.create_source(str(skeleton))
    if not source:
        raise RuntimeError(reason)
    resources = [source, source.get_editor_property("skeleton")]
    skeleton_data = json.loads(skeleton.read_text(encoding="utf-8-sig"))
    profiles = []
    boundaries = {}
    for row in selected:
        body = row['id']
        target = require(row['target_mesh'])
        profile_path = row['profile']
        reason = ue.AetherMotionAuthoring.create_retarget_assets(source, target, str(BINDINGS), body)
        if reason is None or reason:
            raise RuntimeError("重定向作者失败：" + body + " " + str(reason))
        for reverse, retarget_path in [(False, row['forward_retargeter']), (True, row['reverse_retargeter'])]:
            reason = ue.AetherMotionAuthoring.calibrate_retarget(require(retarget_path), source, reverse, str(BINDINGS), body)
            if reason is None or reason:
                raise RuntimeError("重定向根高度校准失败：" + body + " " + str(reason))
        profiles.append(require(profile_path))
        for key in ('profile','forward_retargeter','reverse_retargeter','target_rig','source_rig'):
            resources.append(require(row[key]))
    # 标签保留全部 configured 条目依赖，单独更新一种身体不能丢掉另一种。
    for row in bindings.values():
        if row['state'] != 'configured':
            continue
        paths = [row[key] for key in ('target_mesh','profile','forward_retargeter','reverse_retargeter','preview_idle','walk_animation','attack_animation')]
        paths += [value for key,value in row['animations'].items() if key != 'light'] + row['animations']['light']
        paths += [row['animation_class'].split('.')[0], row['source_animation_class'].split('.')[0]]
        for path in paths:
            resources.append(require(path))
    for clip in sorted((ROOT / "ContentSource/Motion/Clips").glob("*.json")):
        path = OUTPUT + "/Baked/AN_" + clip.stem
        # 基变换校准后所有已管理片段都必须同步，不能混用旧姿态。
        animation, reason = ue.AetherMotionAuthoring.import_clip(str(clip), source, path)
        if not animation:
            raise RuntimeError(reason)
        resources.append(animation)
        data = json.loads(clip.read_text(encoding="utf-8-sig"))
        style_names = {filename: style for row in selected for style,filename in row['styles'].items()}
        style = style_names.get(clip.stem)
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
        profile.set_editor_property("source_x", ue.Vector(0,-1,0))
        profile.set_editor_property("skeleton_sha256", skeleton_data["skeletonSha256"])
        styles = dict(profile.get_editor_property("styles"))
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
    ue.log("动作资源制作完成；该消息不代表动作质量或运行效果已验证。")
if __name__ == "__main__":
    main()
