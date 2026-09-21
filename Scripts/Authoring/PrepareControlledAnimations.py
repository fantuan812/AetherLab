"""UE 编辑器作者入口：官方 Manny/Quinn 骨架、官方源动画和项目自制关节关键帧。
仅生成资产，不启动游戏或将制作结果标记为视觉验收通过。
"""
import hashlib
import json
from pathlib import Path
import unreal as ue

def load_optional(path):
    # 首次创建不存在的目标是正常情况；不要向命令行作者过程记录误导性 Error。
    return ue.EditorAssetLibrary.load_asset(path) if ue.EditorAssetLibrary.does_asset_exist(path) else None


L = ue.EditorAssetLibrary
T = ue.AssetToolsHelpers.get_asset_tools()
ROOT = "/Game/Animation/Controlled"
BASE = "/Game/Characters/Mannequins/Anims/"
IDLE = BASE + "Unarmed/MM_Idle"
WALK = BASE + "Unarmed/Walk/MF_Unarmed_Walk_Fwd"
RECIPES = {}


def key(t, rotation=(0, 0, 0), translation=(0, 0, 0)):
    return [t, list(rotation), list(translation)]


def constant(rotation=(0, 0, 0), translation=(0, 0, 0)):
    return [key(0, rotation, translation), key(1, rotation, translation)]


def curve(values):
    # values: normalized time, rotation, translation。旋转单位为骨骼局部轴角度。
    return [key(*v) for v in values]


def recipe(name, source=IDLE, duration=1.0, animate=False, reverse=False, tracks=None):
    RECIPES[name] = dict(source=source, schema=1, duration=duration,
                         animateSource=animate, reverseSource=reverse, tracks=tracks or {})


def squat(depth=34, bend=1.0):
    return {
        "pelvis": constant(translation=(0, 0, -depth)),
        "thigh_l": constant((-32*bend, 0, 0)),
        "thigh_r": constant((32*bend, 0, 0)),
        "calf_l": constant((64*bend, 0, 0)),
        "calf_r": constant((-64*bend, 0, 0)),
        "foot_l": constant((-32*bend, 0, 0)),
        "foot_r": constant((32*bend, 0, 0)),
        "spine_02": constant((10, 0, 0)),
    }


def arms():
    return {"upperarm_l": constant((0, -45, -35)), "upperarm_r": constant((0, 45, 35)),
            "lowerarm_l": constant((0, -55, 0)), "lowerarm_r": constant((0, 55, 0))}


# 蹲走逐帧保留源动画的交替步态，再烘焙降低骨盆/髋膝踝屈曲；不是静止蹲姿播放。
recipe("CrouchIdle", tracks=squat())
for name, suffix in [("CrouchWalk", "Fwd"), ("CrouchBack", "Bwd"),
                     ("CrouchLeft", "Left"), ("CrouchRight", "Right")]:
    recipe(name, BASE + "Unarmed/Walk/MF_Unarmed_Walk_" + suffix, 1.0, True, tracks=squat(30, .85))
recipe("CarryIdle", tracks=arms())
recipe("CarryWalk", WALK, 1.0, True, tracks=arms())
recipe("Guard", tracks={**arms(), "upperarm_l": constant((0, -68, -55)),
                        "lowerarm_l": constant((0, -85, 0)), "spine_03": constant((0, -8, 0))})

# 四向地面闪避各有重心降低、蹬地、收腿与恢复关键帧；胶囊位移仍由 GAS root source 控制。
for name, side, backward in [("DodgeForward", 0, False), ("DodgeBack", 0, True),
                             ("DodgeLeft", -1, False), ("DodgeRight", 1, False)]:
    lean = -18 if backward else 18
    tracks = {
        "pelvis": curve([(0, (0, 0, 0), (0, 0, 0)), (.2, (0, 0, side*12), (0, 0, -20)),
                         (.65, (0, 0, side*6), (0, 0, -12)), (1, (0, 0, 0), (0, 0, 0))]),
        "spine_02": curve([(0, (0, 0, 0)), (.2, (lean, 0, side*15)), (.65, (lean/2, 0, side*8)), (1, (0, 0, 0))]),
    }
    for bone, sign in [("thigh_l", -1), ("thigh_r", 1), ("calf_l", 1), ("calf_r", -1)]:
        amplitude = 52 if bone.startswith("calf") else 30
        tracks[bone] = curve([(0, (0, 0, 0)), (.25, (sign*amplitude, side*12, 0)),
                              (.65, (-sign*amplitude*.35, 0, 0)), (1, (0, 0, 0))])
    tracks.update(arms())
    recipe(name, duration=.55, tracks=tracks)

reach = {
    "spine_01": curve([(0, (0, 0, 0)), (.4, (28, 0, 0)), (.65, (28, 0, 0)), (1, (0, 0, 0))]),
    "upperarm_l": curve([(0, (0, 0, 0)), (.4, (0, -65, -20)), (.65, (0, -65, -20)), (1, (0, 0, 0))]),
    "upperarm_r": curve([(0, (0, 0, 0)), (.4, (0, 65, 20)), (.65, (0, 65, 20)), (1, (0, 0, 0))]),
}
recipe("Pickup", duration=.7, tracks=reach)
recipe("PutDown", duration=.65, tracks=reach)
recipe("Rescue", duration=1.2, tracks={**squat(40, 1.2), **arms(),
    "spine_02": curve([(0, (18, 0, 0)), (.5, (23, 0, 0)), (1, (18, 0, 0))]),
    "lowerarm_r": curve([(0, (0, 45, 0)), (.5, (0, 65, 0)), (1, (0, 45, 0))])})
recipe("Throw", duration=.65, tracks={
    "spine_02": curve([(0, (0, 0, 0)), (.35, (-15, 0, 0)), (.65, (20, 0, 0)), (1, (0, 0, 0))]),
    "upperarm_l": curve([(0, (0, -45, -35)), (.35, (0, -20, -110)), (.65, (0, -80, -25)), (1, (0, 0, 0))]),
    "upperarm_r": curve([(0, (0, 45, 35)), (.35, (0, 20, 110)), (.65, (0, 80, 25)), (1, (0, 0, 0))])})
recipe("Cast", duration=.8, tracks={
    "upperarm_r": curve([(0, (0, 0, 0)), (.25, (0, 35, 90)), (.6, (0, 65, 20)), (1, (0, 0, 0))]),
    "lowerarm_r": curve([(0, (0, 0, 0)), (.25, (0, 80, 0)), (.6, (0, 15, 0)), (1, (0, 0, 0))]),
    "spine_03": curve([(0, (0, 0, 0)), (.25, (0, -12, 0)), (.6, (0, 15, 0)), (1, (0, 0, 0))])})
recipe("Vault", duration=.82, tracks={
    **reach,
    "pelvis": curve([(0, (0, 0, 0), (0, 0, 0)), (.27, (15, 0, 0), (0, 0, -25)),
                     (.73, (10, 0, 0), (0, 0, -18)), (1, (0, 0, 0), (0, 0, 0))]),
    "thigh_l": curve([(0, (0, 0, 0)), (.27, (-65, 0, 0)), (.73, (-45, 0, 0)), (1, (0, 0, 0))]),
    "thigh_r": curve([(0, (0, 0, 0)), (.27, (65, 0, 0)), (.73, (45, 0, 0)), (1, (0, 0, 0))]),
    "calf_l": curve([(0, (0, 0, 0)), (.27, (95, 0, 0)), (.73, (70, 0, 0)), (1, (0, 0, 0))]),
    "calf_r": curve([(0, (0, 0, 0)), (.27, (-95, 0, 0)), (.73, (-70, 0, 0)), (1, (0, 0, 0))])})
recipe("Stun", duration=1.0, tracks={
    "spine_02": curve([(0, (15, 0, 0)), (.25, (18, 0, 5)), (.75, (18, 0, -5)), (1, (15, 0, 0))]),
    "neck_01": curve([(0, (12, 0, 0)), (.5, (18, 8, 0)), (1, (12, 0, 0))]), **arms()})

# 真实受击/倒地/起身和落地直接使用官方完整序列；起身反向重采样完整倒地运动并保持足 IK 收尾。
recipe("Death", BASE + "Death/MM_Death_Front_01", 1.5, True)
recipe("GetUp", BASE + "Death/MM_Death_Front_01", 1.2, True, True)
recipe("Hit", BASE + "Rifle/HitReact/MM_HitReact_Front_Med_01", .35, True)
recipe("Land", BASE + "Unarmed/Jump/MM_Land", .18, True)
recipe("LandHeavy", BASE + "Unarmed/Jump/MM_Land", .45, True, tracks={
    "pelvis": curve([(0, (0, 0, 0), (0, 0, -15)), (.4, (0, 0, 0), (0, 0, -25)), (1, (0, 0, 0), (0, 0, 0))]),
    "spine_02": curve([(0, (12, 0, 0)), (.4, (24, 0, 0)), (1, (0, 0, 0))])})

L.make_directory(ROOT)
clips = {}
manifest = []
for name, definition in RECIPES.items():
    source_path = definition["source"]
    source = load_optional(source_path)
    if not isinstance(source, ue.AnimSequence):
        raise RuntimeError("缺少官方源动画：" + source_path)
    payload = json.dumps(definition, ensure_ascii=False, sort_keys=True)
    result = ue.AetherAnimationAuthoring.bake_controlled_clip(source, payload, ROOT + "/A_" + name)
    clip, reason = result if isinstance(result, tuple) else (result, "")
    if not clip:
        raise RuntimeError(name + ": " + reason)
    clips[name] = clip
    manifest.append(dict(name=name, source=source_path, recipeSha256=hashlib.sha256(payload.encode()).hexdigest(),
                         target=clip.get_path_name(), visualAcceptance="pending"))
factory = ue.DataAssetFactory()
factory.set_editor_property("data_asset_class", ue.AetherActionSet)
asset = load_optional(ROOT + "/DA_Actions") or T.create_asset("DA_Actions", ROOT, ue.AetherActionSet, factory)
asset.set_editor_property("clips", clips)
if not L.save_loaded_asset(asset):
    raise RuntimeError("无法保存动作集合")
# 所有动画通过 DA_Actions 硬引用，主角色 AnimInstance 的引用使 Cook 可达。
folder = Path(ue.Paths.project_saved_dir()) / "Authoring" / "Controlled"
folder.mkdir(parents=True, exist_ok=True)
(folder / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding="utf-8")
ue.log("V10_CONTROLLED_ANIMATIONS_AUTHORED; visual/network/contact acceptance pending")
