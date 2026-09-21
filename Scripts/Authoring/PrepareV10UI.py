"""制作正式 UI 蓝图、主题和基础几何图标；不把作者成功当作运行验收。"""
import json
import math
import pathlib
import re
import struct
import unreal as ue

root = pathlib.Path(ue.Paths.project_dir())
library = ue.EditorAssetLibrary
tools = ue.AssetToolsHelpers.get_asset_tools()
assets = []
source = (root / "Source/AetherUI/Private/UI/AetherWidgetAssets.cpp").read_text(encoding="utf-8-sig")
rows = re.findall(r'\{TEXT\("(Aether[^"]+)"\),TEXT\("(WBP_[^"]+)"\)\}', source)
rows += [("AetherInventoryCell", "WBP_EquipmentSlot"),
         ("AetherInspectionConfirmation", "WBP_QuantityDialog")]
rows += [("AetherHUDSection", name) for name in ("WBP_Vitals", "WBP_QuestTracker", "WBP_QuickBar", "WBP_InteractionPrompt", "WBP_TargetVitals")]
if len(rows) < 16:
    raise RuntimeError("Widget 资源注册表不完整")
library.make_directory("/Game/UI/Widgets")
for parent_name, name in rows:
    parent = getattr(ue, parent_name, None)
    if parent is None:
        raise RuntimeError("尚未编译原生控件：" + parent_name)
    path = "/Game/UI/Widgets/" + name
    asset = library.load_asset(path) if library.does_asset_exist(path) else None
    if not asset:
        factory = ue.WidgetBlueprintFactory()
        factory.set_editor_property("parent_class", parent)
        asset = tools.create_asset(name, "/Game/UI/Widgets", ue.WidgetBlueprint, factory)
    if not isinstance(asset, ue.WidgetBlueprint):
        raise RuntimeError("Widget 资产类型冲突：" + path)
    # 已有蓝图布局保持，不通过脚本清空设计者的 WidgetTree。
    if not library.save_loaded_asset(asset):
        raise RuntimeError("无法保存控件：" + path)
    assets.append(asset)

# 按子控件依赖顺序编译真实 Designer 布局，父 WBP 引用已经可加载的子 WBP 类。
layout_doc = json.loads((root / "Content/AetherCore/Definitions/V10/WidgetLayouts.json").read_text(encoding="utf-8-sig"))
if layout_doc.get("SchemaVersion") != 1:
    raise RuntimeError("不支持的 Widget 布局版本")
layouts = layout_doc["Layouts"]
finished, visiting = set(), set()
def author_layout(name):
    if name in finished:
        return
    if name in visiting:
        raise RuntimeError("Widget 布局包含循环引用：" + name)
    visiting.add(name)
    spec = layouts[name]
    def dependencies(node):
        path = node["Class"]
        if path.startswith("/Game/UI/Widgets/"):
            child = path.rsplit("/", 1)[-1].split(".", 1)[0]
            if child in layouts:
                author_layout(child)
        for child in node.get("Children", []):
            dependencies(child)
    dependencies(spec)
    asset = library.load_asset("/Game/UI/Widgets/" + name)
    result = ue.AetherWidgetAuthoring.apply_layout(asset, json.dumps(spec, ensure_ascii=False))
    success, reason = result if isinstance(result, tuple) else (bool(result), "")
    if not success:
        raise RuntimeError("布局制作失败：" + name + " " + reason)
    visiting.remove(name)
    finished.add(name)
for layout_name in layouts:
    author_layout(layout_name)

theme_path = "/Game/UI/DA_UITheme"
theme = library.load_asset(theme_path) if library.does_asset_exist(theme_path) else None
if not theme:
    factory = ue.DataAssetFactory()
    factory.set_editor_property("data_asset_class", ue.AetherUITheme)
    theme = tools.create_asset("DA_UITheme", "/Game/UI", ue.AetherUITheme, factory)
if not isinstance(theme, ue.AetherUITheme):
    raise RuntimeError("主题类型冲突")
library.save_loaded_asset(theme)
assets.append(theme)

# 图标使用自制基础几何线条；不下载外部人物、贴图或具有不明许可的图集。
items = json.loads((root / "Content/AetherCore/Definitions/V10/Items.json").read_text(encoding="utf-8-sig"))
skills = json.loads((root / "Content/AetherCore/Definitions/V10/Skills.json").read_text(encoding="utf-8-sig"))
ids = sorted({x["IconId"] for x in items["Items"] + skills["Skills"] if x.get("IconId")})
scratch = root / "Saved/Authoring/UIIcons"
scratch.mkdir(parents=True, exist_ok=True)
glyphs = {
    "Potion": [[(25,12),(39,12),(39,22),(46,40),(43,53),(21,53),(18,40),(25,22),(25,12)],[(25,20),(39,20)]],
    "Material": [[(32,10),(51,24),(47,48),(21,54),(12,32),(32,10)],[(12,32),(32,27),(51,24)],[(32,27),(21,54)]],
    "Weapon": [[(16,50),(47,11),(49,24),(23,48)],[(16,37),(31,49)]],
    "Shield": [[(14,13),(50,13),(48,39),(32,54),(16,39),(14,13)],[(32,17),(32,47)]],
    "Head": [[(14,45),(14,29),(22,15),(42,15),(50,29),(50,45),(40,45),(40,33),(24,33),(24,45),(14,45)]],
    "Chest": [[(21,12),(27,17),(37,17),(43,12),(54,23),(44,31),(44,53),(20,53),(20,31),(10,23),(21,12)]],
    "Hands": [[(17,42),(13,27),(19,25),(24,34),(23,15),(28,15),(31,30),(33,11),(38,12),(39,31),(44,18),(49,21),(46,40),(38,53),(22,53),(17,42)]],
    "Legs": [[(18,12),(46,12),(49,51),(36,51),(32,30),(28,51),(15,51),(18,12)]],
    "Feet": [[(20,12),(42,12),(42,36),(52,43),(52,52),(13,52),(13,41),(20,35),(20,12)]],
    "Neck": [[(15,14),(18,30),(32,45),(46,30),(49,14)],[(32,40),(24,50),(32,58),(40,50),(32,40)]],
    "Ring1": [[(32+18*math.cos(i*math.pi/12),34+18*math.sin(i*math.pi/12)) for i in range(25)],[(24,12),(40,12),(36,23),(28,23),(24,12)]],
    "Ring2": [[(32+18*math.cos(i*math.pi/12),34+18*math.sin(i*math.pi/12)) for i in range(25)]],
    "Fire": [[(32,8),(40,26),(44,19),(51,36),(45,52),(31,57),(18,49),(14,34),(25,20),(24,38),(32,8)]],
    "Water": [[(32,8),(13,38),(16,51),(32,58),(48,51),(51,38),(32,8)]],
    "Frost": [[(10,32),(54,32)],[(32,10),(32,54)],[(16,16),(48,48)],[(16,48),(48,16)]],
    "Storm": [[(37,7),(15,36),(30,36),(25,57),(50,26),(35,26),(37,7)]],
}
def image(icon):
    size = 64
    pixel = bytearray(size * size * 4)
    base = icon.split(".")[0]
    color = {"Fire": (248,113,58), "Water": (69,164,242), "Frost": (144,228,245),
             "Storm": (192,154,249)}.get(base, (230,198,124))
    lines = glyphs.get(base, glyphs["Material"])
    for y in range(size):
        for x in range(size):
            coverage = 0
            for points in lines:
                for a,b in zip(points,points[1:]):
                    dx,dy=b[0]-a[0],b[1]-a[1]
                    t=max(0,min(1,((x+.5-a[0])*dx+(y+.5-a[1])*dy)/max(.001,dx*dx+dy*dy)))
                    distance=math.hypot(x+.5-a[0]-t*dx,y+.5-a[1]-t*dy)
                    coverage=max(coverage,max(0,min(1,2.1-distance)))
            at=(y*size+x)*4
            pixel[at:at+4]=bytes((color[2],color[1],color[0],round(255*coverage)))
    header=struct.pack("<BBBHHBHHHHBB",0,0,2,0,0,0,0,0,size,size,32,0x28)
    return header+pixel

library.make_directory("/Game/UI/Icons")
for icon in ids:
    name="T_"+icon.replace(".","_")
    path="/Game/UI/Icons/"+name
    asset=library.load_asset(path) if library.does_asset_exist(path) else None
    if not asset:
        filename=scratch/(name+".tga")
        filename.write_bytes(image(icon))
        task=ue.AssetImportTask()
        task.set_editor_property("filename",str(filename))
        task.set_editor_property("destination_path","/Game/UI/Icons")
        task.set_editor_property("destination_name",name)
        task.set_editor_property("automated",True)
        task.set_editor_property("save",True)
        tools.import_asset_tasks([task])
        asset=library.load_asset(path)
    if not isinstance(asset,ue.Texture2D):
        raise RuntimeError("图标导入失败："+path)
    asset.set_editor_property("lod_group",ue.TextureGroup.TEXTUREGROUP_UI)
    asset.set_editor_property("compression_settings",ue.TextureCompressionSettings.TC_EDITOR_ICON)
    asset.set_editor_property("mip_gen_settings",ue.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    library.save_loaded_asset(asset)
    assets.append(asset)
factory=ue.DataAssetFactory()
factory.set_editor_property("data_asset_class",ue.PrimaryAssetLabel)
label=library.load_asset("/Game/UI/PAL_UI") or tools.create_asset("PAL_UI","/Game/UI",ue.PrimaryAssetLabel,factory)
rules=label.get_editor_property("rules")
rules.set_editor_property("cook_rule",ue.PrimaryAssetCookRule.ALWAYS_COOK)
label.set_editor_property("rules",rules)
label.set_editor_property("is_runtime_label",True)
label.set_editor_property("explicit_assets",assets)
if not library.save_loaded_asset(label):
    raise RuntimeError("无法保存 UI Cook 标签")
ue.log("V10_UI_ASSETS_AUTHORED; 视觉/输入/发布验收尚未执行")
