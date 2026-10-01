"""作者化正式无持久化开始页；只生成目标资产，不修改默认地图或声明运行验收。

必须在隔离的 UE 作者进程执行。当前实现阶段仅提交源码，未运行本脚本。
"""
import unreal as ue

settings_type = getattr(ue, "AetherStartupSettings", None)
mode_type = getattr(ue, "AetherFrontendMode", None)
if settings_type is None or mode_type is None:
    raise RuntimeError("尚未编译正式 StartupSettings/FrontendMode；不能生成替代模式")

settings = ue.get_default_object(settings_type)
target = settings.get_frontend_asset_path()
# SoftObjectPath 的资产名形式 /Game/.../Map.Map；只接受项目地图包。
if not target.startswith("/Game/") or "?" in target or ".." in target:
    raise RuntimeError("FrontendMap 配置不是有效项目地图软路径")
package = target.rsplit(".", 1)[0] if "." in target.rsplit("/", 1)[-1] else target
library = ue.EditorAssetLibrary

if library.does_asset_exist(package):
    world = library.load_asset(package)
    if not isinstance(world, ue.World):
        raise RuntimeError("FrontendMap 路径已有非地图资产，拒绝覆盖")
    actual = world.get_world_settings().get_editor_property("default_game_mode")
    if actual != mode_type.static_class():
        raise RuntimeError("现有地图不是正式 FrontendMode，拒绝改写作者资产")
else:
    # 现有脏地图不能因自动作者化被丢弃；此工具应只在专门进程里工作。
    if ue.EditorLoadingAndSavingUtils.get_dirty_map_packages():
        raise RuntimeError("当前编辑器地图未保存；请使用隔离作者进程")
    world = ue.EditorLoadingAndSavingUtils.new_blank_map(False)
    if world is None:
        raise RuntimeError("无法创建正式前端地图")
    world.get_world_settings().set_editor_property("default_game_mode", mode_type.static_class())
    if not ue.EditorLoadingAndSavingUtils.save_map(world, package):
        raise RuntimeError("无法保存正式前端地图")

if not library.does_asset_exist(package):
    raise RuntimeError("作者化没有产生可读的原生地图")
if world.get_world_settings().get_editor_property("default_game_mode") != mode_type.static_class():
    raise RuntimeError("前端地图模式核对失败")
ue.log("AETHER_FRONTEND_ASSET_AUTHORED " + package)
ue.log("仅作者化完成；仍需资产审阅和后续明确授权的编译/测试，不代表运行验收。")
