"""v10 角色预览资产制作入口；本轮仅编写，不执行。
全部剩余实现结束、允许统一编译阶段后，在 UE Editor Python 环境运行。
只使用项目已包含的 UE 官方 Manny/Quinn 和基础资源，不导入第三方模型。
"""
import unreal as ue

library = ue.EditorAssetLibrary
tools = ue.AssetToolsHelpers.get_asset_tools()
TAG = "AetherPreviewGenerated"
VERSION = "v10-alpha-1"


def require(path):
    asset = library.load_asset(path)
    if not asset:
        raise RuntimeError("缺少预览资源：" + path)
    return asset


def create_asset(name, folder, cls, factory):
    path = folder + "/" + name
    if library.does_asset_exist(path):
        return require(path), False
    asset = tools.create_asset(name, folder, cls, factory)
    if not asset:
        raise RuntimeError("无法创建资产：" + path)
    return asset, True


def prepare_material():
    # 材质默认参数使用一个很小的线性 HDR RT，避免用 sRGB 白贴图掩盖采样器类型错误。
    template, new_template = create_asset(
        "RT_CharacterPreviewTemplate", "/Game/UI/RenderTargets",
        ue.TextureRenderTarget2D, ue.TextureRenderTargetFactoryNew())
    if new_template:
        template.set_editor_property("size_x", 64)
        template.set_editor_property("size_y", 64)
        template.set_editor_property("render_target_format", ue.TextureRenderTargetFormat.RTF_RGBA16F)
        library.save_loaded_asset(template)

    material, created = create_asset(
        "M_CharacterPreview", "/Game/UI/Materials", ue.Material, ue.MaterialFactoryNew())
    owned = library.get_metadata_tag(material, TAG)
    if not created and owned != VERSION:
        # 手工替换后的材质保持原样；内容验收仍须检查 PreviewTexture 参数与反向 alpha。
        ue.log_warning("保留已有非生成材质：" + material.get_path_name())
        return material, template
    material.set_editor_property("material_domain", ue.MaterialDomain.MD_UI)
    material.set_editor_property("blend_mode", ue.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property("two_sided", True)
    editing = ue.MaterialEditingLibrary
    editing.delete_all_material_expressions(material)
    texture = editing.create_material_expression(material, ue.MaterialExpressionTextureSampleParameter2D, -420, 0)
    texture.set_editor_property("parameter_name", "PreviewTexture")
    texture.set_editor_property("texture", template)
    texture.set_editor_property("sampler_type", ue.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
    alpha = editing.create_material_expression(material, ue.MaterialExpressionOneMinus, -160, 160)
    # SCS_SceneColorHDR 输出反向不透明度；黑底/全透明问题不能用固定 Opacity=1 隐藏。
    if not editing.connect_material_expressions(texture, "A", alpha, "Input"):
        raise RuntimeError("无法连接预览 alpha")
    if not editing.connect_material_property(texture, "RGB", ue.MaterialProperty.MP_EMISSIVE_COLOR):
        raise RuntimeError("无法连接预览颜色")
    if not editing.connect_material_property(alpha, "", ue.MaterialProperty.MP_OPACITY):
        raise RuntimeError("无法连接预览透明度")
    library.set_metadata_tag(material, TAG, VERSION)
    editing.recompile_material(material)
    library.save_loaded_asset(material)
    return material, template


def prepare_body_definitions():
    idle = require("/Game/Characters/Mannequins/Anims/Unarmed/MM_Idle")
    manny = require("/Game/AetherCore/Data/DA_Character_Player")
    manny.set_editor_property("preview_idle_animation", idle)
    library.save_loaded_asset(manny)
    target = "/Game/AetherCore/Data/DA_Character_Player_Quinn"
    if not library.does_asset_exist(target):
        quinn = library.duplicate_asset(manny.get_path_name().split(".")[0], target)
        if not quinn:
            raise RuntimeError("无法制作 Quinn 外观配置")
        quinn.set_editor_property("character_id", "UE_PlayerQuinn")
        quinn.set_editor_property("body_mesh", require("/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple"))
        quinn.set_editor_property("preview_idle_animation", idle)
        library.save_loaded_asset(quinn)
    return manny, require(target), idle


def main():
    material, template = prepare_material()
    manny, quinn, idle = prepare_body_definitions()
    factory = ue.WidgetBlueprintFactory()
    factory.set_editor_property("parent_class", ue.AetherCharacterPreviewWidget)
    widget, _ = create_asset("WBP_CharacterPreview", "/Game/UI/Inventory", ue.WidgetBlueprint, factory)
    library.save_loaded_asset(widget)
    # 显式 Cook 标签把运行时软引用资产纳入包，避免只在编辑器缓存命中时可用。
    factory = ue.DataAssetFactory()
    factory.set_editor_property("data_asset_class", ue.PrimaryAssetLabel)
    label, _ = create_asset("DA_CharacterPreviewCook", "/Game/UI", ue.PrimaryAssetLabel, factory)
    rules = ue.PrimaryAssetRules()
    rules.set_editor_property("cook_rule", ue.PrimaryAssetCookRule.ALWAYS_COOK)
    label.set_editor_property("rules", rules)
    label.set_editor_property("is_runtime_label", True)
    label.set_editor_property("explicit_assets", [material, template, widget, manny, quinn, idle])
    library.save_loaded_asset(label)
    ue.log("角色预览资产已制作；这不是编译、透明度、体型、输入或发布验收记录。")


if __name__ == "__main__":
    main()
