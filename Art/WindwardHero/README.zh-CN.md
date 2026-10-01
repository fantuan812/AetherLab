# 风行者：原创冒险者建模试作

2026-10-01，Blender 5.2.2 LTS。按本次明确要求新增独立人物美术试作，采用塞尔达式卡通冒险气质：尖耳、暖金头发、绿色短袍、皮革护腕与长靴。人物造型、叶片盾徽和装备几何均为原创。

使用 Blender Python 逐块构建网格、安排关节环线和指定蒙皮权重；没有下载现成人物、使用 AI 生成网格或复用任天堂角色资产。源码可以继续编辑，也可以直接在 Blender 中改网格与骨骼。本目录属于美术实验，不代表设计文档中玩家外观、角色系统或 UE 运行时的正式验收。

## 文件

- `WindwardHero.blend`：可编辑模型、原始骨骼、蒙皮、材质、UV、四个动作以及独立摄影棚。
- `Exports/SK_WindwardHero.fbx`：A 姿势骨骼模型，关闭动画导出。
- `Exports/AN_WindwardHero_*.fbx`：每个示范动作一个独立 FBX，包含同一模型与骨架。
- `Exports/WindwardHero.glb`：自包含材质、蒙皮和动作，便于其他工具查看。
- `Previews/`：正面、背面、三分之四视角，行走、挥手、挥剑姿势和骨骼示意。
- `asset_manifest.json`：生成的骨骼、网格和动作数量。
- `workflow_report.json`：按用户指定顺序执行的九步流程与实际输出。
- `Textures/`：五组 BaseColor、Normal、AO 共 15 张 PNG；`.blend` 内已打包。
- `UV/`：五个网格各自的真实 UV 布局 SVG。
- `../../Tools/BlenderMCP/build_windward_hero.py`：建模和导出源码。
- `../../Tools/BlenderMCP/finish_windward_hero.py`：从已有试作拆除旧绑定，再按九步流程重建的源码。
- `../../Tools/BlenderMCP/preview_windward_hero.py`：姿势预览源码。

低模约 1.88 米高；7,691 个网格顶点、14,868 个三角面；五个独立网格分别为身体、头部、头发、剑和盾，均有 UV。隐藏的 `HIGH | editable bake sources` 集合保留 114,449 个顶点的高模烘焙源。贴图最大 2K，当前已制作烘焙贴图与 PBR/皮肤 Shader，尚无手绘贴图和游戏内卡通着色器。

## 本次指定流程

**轮廓拓扑 → 法线 → UV → 高模烘焙 → 材质 → 皮肤 Shader → 毛发 → 骨骼权重 → 动画。**

第一版是造型和绑定试作。收到用户流程要求后，后处理先拆除试作骨架、蒙皮和动作；暂存原始关节区域数据，从网格层按九步顺序重新处理，最后才创建交付骨架、规范化权重和动作。实际顺序与输出见 `workflow_report.json`。

| 步骤 | 本次实际产物 | 仍待打磨 |
|---|---|---|
| 轮廓拓扑 | 原创五网格，手臂、腿和手指有关节环线 | 部分衣物以重叠网格衔接，并非单一闭合人物表面 |
| 法线 | 重算法线朝外、60° 锐边、剑盾加权角点法线 | 更细的硬软边美术调整 |
| UV | 各网格独立打包，导出真实 UV 布局 | 当前自动分岛，需要手工接缝优化 |
| 高模烘焙 | 细分高模与几何褶皱/皮革颗粒，真实高到低切线法线与局部 AO | 高模细节是程序构建，没有宣称完成手工雕刻或生产级投射笼审查 |
| 材质 | 五张 BaseColor 图集，布料、皮革、木、铜与钢的独立参数 | 手绘风格纹理与 UE 材质制作 |
| 皮肤 Shader | Blender Principled 次表面散射，皮肤与眼睛分别调参 | UE 皮肤材质、面部表情与更细肤色层次 |
| 毛发 | 独立分束多边形头发、图集、各向异性高光与 Sheen | 没有 strand groom 或毛发模拟 |
| 骨骼权重 | 重建 65 骨，全部 7,691 顶点权重规范化，最多 2 个影响 | 更极端姿势下的变形打磨与可选 IK 校正 |
| 动画 | 四个 FK 示范动作及姿势渲染 | 脚底锁定、根运动和游戏动作接入 |

烘焙使用 [Blender 官方高到低烘焙流程](https://docs.blender.org/manual/en/5.2/render/cycles/baking.html)，法线为切线空间 **+Y**，AO 取自高模局部几何。皮肤采用 [Principled BSDF 次表面散射](https://docs.blender.org/manual/en/5.2/render/shader_nodes/shader/principled.html)，权重 0.10、尺度 0.012 米；这些 Blender 节点设置不代表 UE 同款 Shader 已完成。

## 骨骼与动作

65 根骨骼，其中 61 根变形骨骼、4 根可选腿部 IK 控制骨骼。包括根骨、骨盆、三节脊柱、颈、头、锁骨、手臂、手掌、左右手五指各三节、腿、脚和脚趾，以及短袍、披肩、剑与盾的骨骼。

打开 `.blend`，选中 `Windward_Rig`，切换 **Pose Mode**。旋转身体或手指骨骼即可摆姿势，再按 `I` 插入关键帧。骨架默认前置显示；身体、头发、装备分开，便于隐藏或调整。

在 **Dope Sheet → Action Editor** 选择动作后，按空格播放。全部为 30 fps：

| 动作 | 帧范围 | 用途 |
|---|---|---|
| `Idle_Breathe` | 1–61 | 待机呼吸、轻微转头与披肩运动 |
| `Walk_InPlace` | 1–31 | 原地走路、手臂摆动和短袍摆动 |
| `Wave_Hello` | 1–81 | 抬右手、挥手和收回；播放时隐藏剑 |
| `Sword_Slash` | 1–41 | 蓄势、挥剑、跟随和回收 |

动作均是可编辑 FK 示范。可选腿部 IK 位于 `calf_l/r` 的 IK 约束，默认 influence 为 0；若需要尝试 IK，将其设为 1，再移动 `CTRL_foot_l/r` 与 `CTRL_knee_l/r`。当前示范动作不使用 IK，脚底锁定、走路节奏和挥剑握持仍需要后续动画打磨。骨骼预览把真实姿态中的骨段向镜头偏移以便看清，是示意图，未修改实际骨架。

## 导出与 UE 边界

FBX 使用米制源文件、单位换算、Z 向上、关闭叶骨；动作逐帧烘焙，分文件导出。对应选项见 [Blender 官方 FBX 文档](https://docs.blender.org/manual/en/5.2/files/import_export/fbx_legacy.html)。

这是原创骨架，骨骼命名接近 UE 常见结构，但不等同于 Manny/Quinn；使用工程既有动画需要另建重定向配置。导入 UE 时先把 `SK_` 文件导入为新的 Skeletal Mesh/Skeleton，再把 `AN_` 文件作为动画导入并选择这一 Skeleton。该流程仅为交接说明，本次未执行 UE 导入。

尚未制作面部表情骨骼、BlendShape、布料模拟、LOD、碰撞与 Physics Asset，也未接入项目角色蓝图、装备系统或 Animation Blueprint。

## 本次实际检查

- Blender 已生成、保存包含高低模、打包贴图和绑定的 `.blend`，导出静态 FBX、四份动作 FBX 和 GLB。
- 已渲染模型三视角、三个骨骼驱动姿势及骨骼示意，并根据画面修订肩部过渡、发束、握持和手臂摆动。
- 这属于 Blender 美术查看，不是 UE 玩法、性能或导入验收。
- **UE 未编译、未测试**：未运行规则测试、游戏启动、Cook 或多人测试；现有游戏角色占位资源仍由工程原流程使用。

## 重建

从项目根目录运行（Blender 路径按实际安装修改）：

```powershell
& 'E:\steam\steamapps\common\Blender\blender.exe' --background --factory-startup --python 'Tools\BlenderMCP\build_windward_hero.py' -- --no-render
& 'E:\steam\steamapps\common\Blender\blender.exe' --background --factory-startup --python 'Tools\BlenderMCP\finish_windward_hero.py'
& 'E:\steam\steamapps\common\Blender\blender.exe' --background --factory-startup --python 'Tools\BlenderMCP\preview_windward_hero.py'
```

脚本只重建 `Art/WindwardHero` 目录的同名资产；若手工编辑源文件，请先另存副本，避免再次执行生成脚本覆盖自己的修改。摄影棚不导出到 FBX/GLB。
