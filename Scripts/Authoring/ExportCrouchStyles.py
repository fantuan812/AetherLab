"""官方动画派生蹲姿 -> Manny 到 G1 离线重定向 -> 有来源摘要的 30 FPS 姿态文件。"""
import pathlib
import unreal as ue

def load_optional(path):
    # 首次创建不存在的目标是正常情况；不要向命令行作者过程记录误导性 Error。
    return ue.EditorAssetLibrary.load_asset(path) if ue.EditorAssetLibrary.does_asset_exist(path) else None

root = pathlib.Path(ue.Paths.project_dir())
lib = ue.EditorAssetLibrary
source = load_optional("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple")
target = load_optional("/Game/Animation/Motion/SK_G1MotionSource")
retargeter = load_optional("/Game/Animation/Motion/RTG_Manny_G1")
if not source or not target or not retargeter:
    raise RuntimeError("先完成 ControlledAnimations 和 MotionAssets 作者步骤")
folder = root / "ContentSource/Motion/Clips"
folder.mkdir(parents=True, exist_ok=True)
for name, style in [("CrouchIdle", "crouch_idle"), ("CrouchWalk", "crouch")]:
    asset_path = "/Game/Animation/Controlled/A_" + name
    source_clip = load_optional(asset_path)
    animation, reason = ue.AetherMotionAuthoring.retarget_clip(source_clip, source, target, retargeter,
        "/Game/Animation/Motion/Authored/G1_A_" + name)
    if not animation:
        raise RuntimeError(reason)
    reason = ue.AetherMotionAuthoring.export_clip(animation, target,
        str(root / "ContentSource/Motion/G1Skeleton.json"), str(folder / (style + ".json")))
    if reason is None:
        raise RuntimeError("G1 姿态导出失败：" + name)
ue.log("V10_CROUCH_POSES_EXPORTED; next run MotionAuthor style and StageMotionRuntime")
