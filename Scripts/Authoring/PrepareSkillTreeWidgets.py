"""v10 技能树与详情 Blueprint 制作入口（本轮只编写，不执行）。
新原生类可用后在 UE Editor Python 环境运行；不导入第三方人物或贴图。
已有资产保留，不覆盖设计者的蓝图修改。
"""
import unreal as ue

LIB = ue.EditorAssetLibrary
TOOLS = ue.AssetToolsHelpers.get_asset_tools()


def create_widget(name, folder, parent_name):
    path = folder + "/" + name
    parent = getattr(ue, parent_name, None)
    if parent is None:
        raise RuntimeError("原生控件尚不可用：" + parent_name)
    if LIB.does_asset_exist(path):
        asset = LIB.load_asset(path)
        if not asset:
            raise RuntimeError("现有控件无法载入：" + path)
        return asset
    factory = ue.WidgetBlueprintFactory()
    factory.set_editor_property("parent_class", parent)
    asset = TOOLS.create_asset(name, folder, ue.WidgetBlueprint, factory)
    if not asset:
        raise RuntimeError("无法创建控件：" + path)
    LIB.save_loaded_asset(asset)
    return asset


def main():
    assets = [
        create_widget("WBP_SkillTree", "/Game/UI/Skills", "AetherSkillTreePage"),
        create_widget("WBP_InspectionCard", "/Game/UI/Inspection", "AetherInspectionCard"),
        create_widget("WBP_InspectionConfirmation", "/Game/UI/Inspection", "AetherInspectionConfirmation"),
    ]
    path = "/Game/UI/DA_SkillTreeCook"
    if LIB.does_asset_exist(path):
        label = LIB.load_asset(path)
    else:
        factory = ue.DataAssetFactory()
        factory.set_editor_property("data_asset_class", ue.PrimaryAssetLabel)
        label = TOOLS.create_asset("DA_SkillTreeCook", "/Game/UI", ue.PrimaryAssetLabel, factory)
    if not label:
        raise RuntimeError("无法创建技能页 Cook 标签")
    # 合并显式资产，不删除设计者已加入的依赖。
    explicit = list(label.get_editor_property("explicit_assets"))
    for asset in assets:
        if asset not in explicit:
            explicit.append(asset)
    rules = label.get_editor_property("rules")
    rules.set_editor_property("cook_rule", ue.PrimaryAssetCookRule.ALWAYS_COOK)
    label.set_editor_property("rules", rules)
    label.set_editor_property("is_runtime_label", True)
    label.set_editor_property("explicit_assets", explicit)
    LIB.save_loaded_asset(label)
    ue.log("技能页与详情 Blueprint 已制作；未声明视觉、联机或发布验收通过。")


if __name__ == "__main__":
    main()
