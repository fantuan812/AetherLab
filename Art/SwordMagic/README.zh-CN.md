# 余烬誓约：断钟修道院与角色原型

**0.4 更新**：新版本位于 `Modular/BrokenBellAbbey_Modular.blend`。身体与武器已经分离，新增行走/攻击原型动画，并导入 `Content/SwordMagic` 接入 UE 换装、战斗与任务。运行 `Scripts/PlayAdventure.ps1`。详见 `Docs/ModularGameplay.zh-CN.md`。下文记录保留的原始展示版，其合体武器与未接入状态不代表 Modular 版本。

2026-09-12。按本次用户改用 MCP 的要求，通过真实 Blender MCP `initialize → tools/list → execute_blender_code` 建模、修正并渲染。没有下载外部模型；没有覆盖之前的 `Art/AetherLab` 模型包。

## 打开成果

- `BrokenBellAbbey_Characters.blend`：可编辑源文件，包含两个场景。
- `SM_01_BrokenBellAbbey`：关卡总览，457 个环境网格，另有 2 个角色摆放参考。
- `SM_02_Characters`：游誓者与铸钟骑士的骨架模型和展示台。
- `Previews/01_Abbey_Overview.png`：关卡全景。
- `Previews/02_Character_Lineup.png`：剑盾游誓者、战锤铸钟骑士。
- `Previews/03_Courtyard_Detail.png`：庭院近景。

Blender 顶部场景下拉菜单可切换两个场景。原始默认场景保留；打开交付文件时默认显示修道院场景。

## 关卡内容与玩法位置

本版是紧凑的空间原型，地块设计为 48 × 76 米，计入边缘石块后的实际范围约 48.6 × 78 米。用于评估移动比例、战斗空间与地标，尚未进行 15 分钟关卡时长测试。

| 区域 | 已建模型 | 对应玩法 |
|---|---|---|
| 灰渡入口与浅渠 | 入口道路、分离桥板、支梁与绳索、冰面、侧路 | 剑断绳、修桥、凝霜过水、干燥绕行的位置预留 |
| 外庭 | 拱门、院墙、扶壁、木箱、导电积水与铜条 | 剑盾交锋、木材燃烧、水雷风险与退路 |
| 回廊 | 双侧拱廊、屋顶、蓄水池、学徒占位、旧誓记录台 | 救人、有限水源、Boss 线索 |
| 钟炉庭院 | 高台阶梯、誓印环、钟楼、悬钟、战旗、古印祭台 | 铸钟骑士战斗与古印处置的位置预留 |
| 返回侧路 | 见证小钟、侧门、干燥石路 | 解除旧誓与返回捷径的位置预留 |

水、冰、火光是可编辑几何表现；没有在 Blender 中运行燃烧、导电、冻结、断桥机关或 AI。`ReactivePreset` 和说明属性只是接入提示，需要绑定项目现有 ReactiveWorld/GAS 逻辑。

## 角色

| 文件 | 几何三角面 | 骨骼 | 模型最高点 |
|---|---:|---:|---:|
| `SK_Oathwanderer` | 3,052 | 17 | 1.8685 米，包含剑尖/头饰 |
| `SK_BellKnight_Auren` | 3,608 | 17 | 3.3129 米，包含王冠 |

骨架包括根、髋、脊柱、颈、头、双腿与双臂。采用装甲分件的刚性权重，每个顶点权重归一化，前臂骨骼驱动已实测。尚未制作动画、手指、面部骨架、IK 控制器、柔性布料或最终变形蒙皮；不是 UE 默认角色骨架的直接替换件。武器与盾牌在本版角色网格内，通过手骨驱动。

## 导出与 UE 接入边界

`Exports/FBX` 和 `Exports/GLB` 各含 3 个文件：完整关卡布局、游誓者、铸钟骑士。模型以米制作，Z 向上。两个角色的 FBX 已通过重新导入，确认 17 骨骨架和米制高度正确。

关卡 FBX 是保留场景位置的多网格布局，适合场景导入或继续拆分；并非全部以原点保存的模块化资产库。网格未附加 UCX 碰撞体、导航、Physics Asset 或 Chaos Geometry Collection。水面不应设置为阻挡 Pawn 的碰撞；冰面是否启用碰撞应由玩法状态控制。角色导入时应创建各自骨架，再进行重定向或制作动画。

角色含 UV0 和 UV1；环境 UV0 为重复平铺的盒式投影，供原型材质使用。使用烘焙光照前需要制作或生成不重叠的环境 Lightmap UV。FBX、GLB 中的基础 PBR 材质与 Blender 渲染可能因引擎光照出现差异。

这批新模型尚未导入 UE 地图，也未替换之前 B–E 灰盒里的角色或环境。现有工程和旧模型均保留。

## 验证和制作记录

- `asset-manifest.json`：环境、角色尺寸和三角面清单。
- `validation.json`：有限坐标、闭合网格、UV、权重、实际姿态驱动与导出文件检查。
- `fbx-roundtrip-validation.json`：FBX 重新导入后的网格数、骨架和米制比例。
- `build-status.json`、`render-status.json`：生成与渲染状态。
- 构建脚本：`Tools/BlenderMCP/build_sword_magic.py`，由 `dispatch_sword_magic.py` 通过 MCP 排入 Blender 主线程。
- 本次修正：`refine_sword_magic.py`，同样通过 MCP 执行。
- 渲染脚本：`render_sword_magic.py`，通过 MCP 调度真实 Blender Cycles 渲染。

重建会更新本包源文件和导出；手工修改后请另存，以保留自己的编辑。
