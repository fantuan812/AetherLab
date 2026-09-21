"""十槽官方基础几何体装备。全部代码完成并具备新 UClass 后才运行；脚本不等于已制作资产。"""
import unreal as ue

L = ue.EditorAssetLibrary
tools = ue.AssetToolsHelpers.get_asset_tools()
folder = "/Game/AetherCore/Data"
catalog = L.load_asset(folder + "/DA_EquipmentCatalog")
cube = L.load_asset("/Engine/BasicShapes/Cube")
if not catalog or not cube:
    raise RuntimeError("缺少官方基础装备目录或 Cube")
# Grip 数据以厘米为单位；小饰品无世界网格，避免为了可见性放大戒指。
rows = [
    ("LeatherCap", ["Head"], "head", (0, 0, 4), (.22, .24, .12), False),
    ("LeatherVest", ["Chest"], "spine_03", (0, 0, 0), (.32, .23, .4), False),
    ("LeatherGloves", ["Hands"], "hand_r", (0, 0, 0), (.1, .07, .15), False),
    ("LeatherLeggings", ["Legs"], "pelvis", (0, 0, -8), (.29, .22, .25), False),
    ("LeatherBoots", ["Feet"], "foot_r", (0, 0, 0), (.13, .25, .12), False),
    ("CopperNecklace", ["Neck"], "neck_01", (0, 0, 0), (1, 1, 1), True),
    ("CopperRing", ["Ring1", "Ring2"], "hand_r", (0, 0, 0), (1, 1, 1), True),
]
# 第二套仍采用 UE 基础几何体。装备定义与背包物品使用相同稳定 ID。
rows += [
    ("IronHelm", ["Head"], "head", (0, 0, 5), (.24, .26, .16), False),
    ("IronCuirass", ["Chest"], "spine_03", (0, 0, 0), (.35, .26, .42), False),
    ("IronGauntlets", ["Hands"], "hand_r", (0, 0, 0), (.12, .09, .17), False),
    ("IronGreaves", ["Legs"], "pelvis", (0, 0, -8), (.32, .25, .28), False),
    ("IronBoots", ["Feet"], "foot_r", (0, 0, 0), (.15, .27, .14), False),
    ("SilverNecklace", ["Neck"], "neck_01", (0, 0, 0), (1, 1, 1), True),
    ("SilverRing", ["Ring1", "Ring2"], "hand_r", (0, 0, 0), (1, 1, 1), True),
]
existing = list(catalog.get_editor_property("items"))
for name, slots, socket, position, scale, invisible in rows:
    path = folder + "/DA_" + name
    item = L.load_asset(path) if L.does_asset_exist(path) else None
    if item and not isinstance(item, ue.AetherEquipmentDefinition):
        raise RuntimeError("资产类型冲突：" + path)
    if not item:
        factory = ue.DataAssetFactory()
        factory.set_editor_property("data_asset_class", ue.AetherEquipmentDefinition)
        item = tools.create_asset("DA_" + name, folder, ue.AetherEquipmentDefinition, factory)
    if not item:
        raise RuntimeError("无法创建：" + path)
    for key, value in dict(item_id=name, display_name=name, slot=slots[0], allowed_slots=slots,
                           socket=socket, mesh=None if invisible else cube,
                           invisible_accessory=invisible, occupies_both_hands=False, allows_guard=False,
                           attacks=[], grip_transform=ue.Transform(location=ue.Vector(*position),
                           scale=ue.Vector(*scale))).items():
        item.set_editor_property(key, value)
    secondary = "hand_l" if name in ("LeatherGloves", "IronGauntlets") else "foot_l" if name in ("LeatherBoots", "IronBoots") else ""
    item.set_editor_property("secondary_socket", secondary)
    item.set_editor_property("secondary_grip_transform", ue.Transform(location=ue.Vector(*position), scale=ue.Vector(*scale)))
    if not L.save_loaded_asset(item):
        raise RuntimeError("无法保存：" + path)
    existing = [old for old in existing if str(old.get_editor_property("item_id")) != name]
    existing.append(item)
# 武器复制已校准的官方基础动作/握持数据，避免制作另一条伤害或动画配置路径。
for name, source, display in [
        ("IronSword", "TrainingSword", "铁剑"), ("IronShield", "TrainingShield", "铁盾")]:
    source_asset = next((a for a in existing if str(a.get_editor_property("item_id")) == source), None)
    if not source_asset:
        raise RuntimeError("缺少武器模板：" + source)
    path = folder + "/DA_" + name
    item = L.load_asset(path) if L.does_asset_exist(path) else L.duplicate_asset(source_asset.get_path_name(), path)
    if not isinstance(item, ue.AetherEquipmentDefinition):
        raise RuntimeError("武器资产类型冲突：" + path)
    for key in ("slot", "allowed_slots", "socket", "mesh", "grip_transform", "secondary_socket",
                "secondary_grip_transform", "invisible_accessory", "occupies_both_hands",
                "allows_guard", "guard_stamina_multiplier", "parry_window_seconds", "attacks"):
        item.set_editor_property(key, source_asset.get_editor_property(key))
    item.set_editor_property("item_id", name)
    item.set_editor_property("display_name", display)
    if not L.save_loaded_asset(item):
        raise RuntimeError("无法保存武器：" + path)
    existing = [a for a in existing if str(a.get_editor_property("item_id")) != name] + [item]
catalog.set_editor_property("items", existing)
if not L.save_loaded_asset(catalog):
    raise RuntimeError("无法保存十槽目录")
ue.log("V10_EQUIPMENT_ASSETS_AUTHORED; Manny/Quinn 握持、双侧防具和穿插仍需统一视觉检查")
