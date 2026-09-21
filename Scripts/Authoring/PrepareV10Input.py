"""从正式角色绑定声明制作 Enhanced Input 资产；不执行游戏或规则测试。"""
import pathlib
import re
import unreal as ue

def load_optional(path):
    # 首次创建不存在的目标是正常情况；不要向命令行作者过程记录误导性 Error。
    return ue.EditorAssetLibrary.load_asset(path) if ue.EditorAssetLibrary.does_asset_exist(path) else None


root = pathlib.Path(ue.Paths.project_dir())
source = (root / "Source/AetherGameplay/Private/Input/AetherPlayerInputComponent.cpp").read_text(encoding="utf-8-sig")
# 绑定声明是动作标识和默认键的唯一作者来源；相同动作的 Started/Completed 合并为一项。
rows = dict(re.findall(r'Bind\("([^"]+)",EKeys::([A-Za-z0-9_]+),', source))
rows.update(Forward="W", Backward="S", Left="A", Right="D",
            LookX="MouseX", LookY="MouseY", PadMoveX="Gamepad_LeftX", PadMoveY="Gamepad_LeftY",
            PadLookX="Gamepad_RightX", PadLookY="Gamepad_RightY")
if len(rows) < 40:
    raise RuntimeError("输入声明不完整，拒绝覆盖资产")
folder = "/Game/AetherCore/Input"
library = ue.EditorAssetLibrary
tools = ue.AssetToolsHelpers.get_asset_tools()
library.make_directory(folder)
def data_factory(cls):
    factory = ue.DataAssetFactory()
    factory.set_editor_property("data_asset_class", cls)
    return factory

actions = {}
for name, key in rows.items():
    path = folder + "/IA_" + name
    action = load_optional(path) if library.does_asset_exist(path) else tools.create_asset(
        "IA_" + name, folder, ue.InputAction, data_factory(ue.InputAction))
    if not isinstance(action, ue.InputAction):
        raise RuntimeError("InputAction 类型冲突：" + path)
    action.set_editor_property("value_type", ue.InputActionValueType.AXIS1D
                              if name in ("LookX", "LookY", "PadMoveX", "PadMoveY", "PadLookX", "PadLookY") else ue.InputActionValueType.BOOLEAN)
    action.set_editor_property("consume_input", False)
    if not library.save_loaded_asset(action):
        raise RuntimeError("无法保存：" + path)
    actions[name] = action
path = folder + "/IMC_Gameplay"
context = load_optional(path) if library.does_asset_exist(path) else tools.create_asset(
    "IMC_Gameplay", folder, ue.InputMappingContext, data_factory(ue.InputMappingContext))
if not isinstance(context, ue.InputMappingContext):
    raise RuntimeError("MappingContext 类型冲突")
context.unmap_all()
for name, key in rows.items():
    input_key = ue.Key()
    input_key.set_editor_property("key_name", key)
    context.map_key(actions[name], input_key)
# 特定设备附加映射与 Chord 由角色安装到上下文副本；不修改已烘焙的默认资产。
if not library.save_loaded_asset(context):
    raise RuntimeError("无法保存游戏输入上下文")
label_path = folder + "/PAL_Input"
factory = ue.DataAssetFactory()
factory.set_editor_property("data_asset_class", ue.PrimaryAssetLabel)
label = load_optional(label_path) if library.does_asset_exist(label_path) else tools.create_asset(
    "PAL_Input", folder, ue.PrimaryAssetLabel, factory)
label.set_editor_property("is_runtime_label", True)
label.set_editor_property("explicit_assets", list(actions.values()) + [context])
rules = label.get_editor_property("rules")
rules.set_editor_property("cook_rule", ue.PrimaryAssetCookRule.ALWAYS_COOK)
label.set_editor_property("rules", rules)
library.save_loaded_asset(label)
ue.log("V10_INPUT_AUTHORED; 尚未运行键鼠/手柄导航验收")
