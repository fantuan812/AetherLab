"""制作正式人物/G1 动画蓝图；原生代理提供图逻辑，资产记录骨架和可编辑继承边界。"""
import unreal as ue

def load_optional(path):
    # 首次创建不存在的目标是正常情况；不要向命令行作者过程记录误导性 Error。
    return ue.EditorAssetLibrary.load_asset(path) if ue.EditorAssetLibrary.does_asset_exist(path) else None

lib = ue.EditorAssetLibrary
tools = ue.AssetToolsHelpers.get_asset_tools()
definitions = [
    ("/Game/Animation/ABP_AetherCharacter", ue.AetherAnimInstance,
     "/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple"),
    ("/Game/Animation/Motion/ABP_G1MotionSource", ue.AetherMotionSourceAnimInstance,
     "/Game/Animation/Motion/SK_G1MotionSource"),
]
assets = []
for path, parent, mesh_path in definitions:
    mesh = load_optional(mesh_path)
    if not mesh:
        raise RuntimeError("缺少骨架源：" + mesh_path)
    asset = load_optional(path)
    if not asset:
        factory = ue.AnimBlueprintFactory()
        factory.set_editor_property("parent_class", parent)
        factory.set_editor_property("target_skeleton", mesh.get_editor_property("skeleton"))
        name = path.rsplit("/", 1)[1]
        asset = tools.create_asset(name, path.rsplit("/", 1)[0], ue.AnimBlueprint, factory)
    if not isinstance(asset, ue.AnimBlueprint):
        raise RuntimeError("动画蓝图资产冲突：" + path)
    ue.BlueprintEditorLibrary.compile_blueprint(asset)
    if not lib.save_loaded_asset(asset):
        raise RuntimeError("保存动画蓝图失败：" + path)
    assets.append(asset)
factory = ue.DataAssetFactory()
factory.set_editor_property("data_asset_class", ue.PrimaryAssetLabel)
label = load_optional("/Game/Animation/PAL_Animation") or tools.create_asset("PAL_Animation", "/Game/Animation", ue.PrimaryAssetLabel, factory)
rules = label.get_editor_property("rules")
rules.set_editor_property("cook_rule", ue.PrimaryAssetCookRule.ALWAYS_COOK)
label.set_editor_property("rules", rules)
label.set_editor_property("is_runtime_label", True)
label.set_editor_property("explicit_assets", assets)
lib.save_loaded_asset(label)
ue.log("V10_ANIMATION_BLUEPRINTS_AUTHORED; pose/render acceptance pending")
