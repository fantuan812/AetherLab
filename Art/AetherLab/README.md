# AetherLab 原型模型包

针对现有 ReactiveWorld 的 Wood / Metal / Water / Oil / Stone 材料体系制作。全部为本项目原创的低多边形几何资产，没有外部模型下载依赖。

- 53 个独立模型，53 个 FBX、53 个 GLB；源几何合计 68,736 个三角面。
- `AetherLab_Prototype.blend`：可编辑模型源文件，以及用 78 个模型实例搭出的实验场。隐藏的 `SM_*` 对象是原点处的独立资产，已标记为 Blender Asset；`Scene_*` 是场景实例。
- `Exports/FBX`：UE 导入源文件，包含 UCX 凸碰撞体。
- `Exports/GLB`：便于其他工具预览的独立模型，携带基础 PBR 材质。Blender 中的程序噪声不包含在 GLB 内。
- `Previews/Prototype_Yard.png`：实验场渲染图。
- `Previews/Asset_Catalog.png`：53 个模型的全尺寸目录。
- `asset-manifest.json`：逐模型的尺寸、三角面、材质槽、用途与场景摆放数据。
- `geometry-validation.json`、`unreal-import-report.json`：实际生成的验证记录。

## 内容范围

| 类别 | 资产 |
|---|---|
| 木制结构 | 木板、木梁、木墙、木门、平台、台阶 |
| 建筑与导体 | 混凝土地板/隔墙、钢柱、波纹屋顶、格栅、栏杆、铁轨、直管、弯管 |
| 交互道具 | 木箱、木桶、柴堆、油桶、水桶、提水桶、储水罐、密闭压力罐、线缆、火盆 |
| 实验装置 | 电极、遗迹控制台、以太反应堆 |
| 水与冰 | 水洼、冻结水洼、油面、冰墙、冰桥 |
| 破损资产 | 炭化木梁、破裂油桶、3 种木碎片、3 种混凝土碎块、3 种冰碎片 |
| 测试靶 | 金属哨兵、木制巨像 |
| 施法模型 | 法杖、护手、热/水/冰/电四种核心、施法环 |

测试靶和护手是静态原型模型，不含骨架、蒙皮、动作或角色控制器。火焰、烟、雨和蒸汽需由 Niagara 表现；法术核心是可选网格，并不替代粒子系统。

## UE 5.8 中使用

导入目标为 `/Game/AetherLabPrototype`，对应项目的 `Content/AetherLabPrototype`。

打开 `/Game/AetherLabPrototype/Maps/L_AetherLab_AssetPreview` 查看资产摆放。此地图是资产预览场景，尚未连接施法、反应组件、破碎切换或敌人逻辑。

资产按 Structure / Props / Mechanisms / Liquids / Damage / Targets / SpellMeshes 分目录。材质为 `M_Aether_Surface` 和 20 个材质实例，保留 `Wetness`、`BurnAmount`、`FrostAmount` 参数供动态材质驱动。UE 材质重新构建了程序噪声，外观与 Blender 渲染近似。

1. 给实际交互 Actor 添加现有的 `ReactiveBodyComponent`，按清单设置 `Preset`。
2. 木墙/木箱是组合静态网格；需要逐块燃烧和坍塌时，用独立木梁/木板装配多个 Actor。UCX 仅定义碰撞，不等于 Chaos Geometry Collection。
3. 水和油面没有阻挡碰撞；冰面有凸碰撞。水洼/冰面使用相同轮廓，按冻结状态切换网格，并按现有组件规则控制 Pawn 碰撞。
4. 压力罐需单独设置 `ReactiveMaterialAsset` 的密闭体积和水量；网格本身不携带压力求解。
5. 油桶的 `Oil` 标签表达桶内燃料；需要分别模拟桶壁和内容物时拆成两个反应体。
6. 根据反应事件切换炭化木梁、破裂油桶、碎片，或者在 UE 中另外建立 Geometry Collection。

Blender 单位为米，Z 向上；FBX 已写入单位转换，UE 导入比例为 1，2m 木板应得到 200cm 高度。建筑以地面中心为原点，地板顶面 Z=0。直管以入口为原点；法术球以球心为原点；门轴建议在 Actor 内偏移到左侧 50cm。

## 重建

脚本均位于 `Tools/BlenderMCP`。重建会覆盖本模型包的生成结果；编辑过的源文件请另存。

```powershell
# Blender 模型与导出
& 'E:\steam\steamapps\common\Blender\blender.exe' --background --python 'C:\ueproject\test\Tools\BlenderMCP\build_assets.py'

# 独立几何验证
& 'E:\steam\steamapps\common\Blender\blender.exe' --background --factory-startup --python 'C:\ueproject\test\Tools\BlenderMCP\validate_blender.py'

# 渲染总览与目录
& 'E:\steam\steamapps\common\Blender\blender.exe' --background --factory-startup --python 'C:\ueproject\test\Tools\BlenderMCP\render_previews.py'
```

UE 导入脚本为 `Tools/BlenderMCP/import_unreal.py`。需要临时启用 PythonScriptPlugin、EditorScriptingUtilities；不要求修改项目的插件配置。MCP 安装和连接说明见 `Tools/BlenderMCP/README.md`。
