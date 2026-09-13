# AetherLab · 剑与魔法反应世界 · UE 5.8

项目方向为第三人称剑与魔法动作冒险，工作名《余烬誓约》：玩家以游誓者的身份，用剑术、咒术与环境互动探索诸国和古老遗迹。

当前版本为 **0.4 模块化剑魔原型**：Blender 角色与修道院已接入 UE，身体、剑、盾、锤分别成件，装备数据驱动攻击与换装，接入 GAS、物理反应、任务和 v2 存档。原型动画、正式美术及生产网络预算仍需继续完善。

- **[模块化玩法与扩展指南](Docs/ModularGameplay.zh-CN.md)**：依赖方向、装备契约、新武器/角色接入和具体限制。

- **[B–E 实施与启动说明](Docs/Implementation-BCDE.zh-CN.md)**：本轮代码、控制方式、验收结果与未完成项。

- [玩法设计](Docs/GameDesign.zh-CN.md)：剑盾、六艺咒术、角色成长、敌人、探索、资源与 UE 接口。
- [世界设定集](Docs/WorldBible.zh-CN.md)：历史、魔法与誓约、诸国势力、主要人物和主线。
- [断钟修道院切片](Docs/VerticalSlice.zh-CN.md)：约 15 分钟关卡、Boss 三条完成路线、资产缺口与制作验收。

- [完整架构设计](Docs/Architecture.zh-CN.md)：模块、数据、热/电/相变/结构、调度、线程、网络和存档设计。
- [运行与接入指南](Docs/Integration.zh-CN.md)：UE 组件、材料资产、Blueprint/C++ 接口。
- [验证记录](Docs/Verification.zh-CN.md)：实际编译和测试结果。
- [实验场截图](Docs/Images/AetherLab.png)：由 UE 5.8 实际渲染的调试视图。
- [修道院灰盒截图](Docs/Images/AetherAdventure.png)：0.3 可玩场景的实际渲染。
- [模块化角色与关卡截图](Docs/Images/SwordMagic_Gameplay.png)：0.4 的 UE 实际画面。
- [拆分后的 Blender 源文件](Art/SwordMagic/Modular/BrokenBellAbbey_Modular.blend)：角色、独立装备与修道院。

运行新修道院：执行 `Scripts/PlayAdventure.ps1`；加 `-Graybox` 运行旧灰盒。WASD 移动、鼠标瞄准、左键轻击、Shift 重击、右键格挡、空格闪避；R 换剑/锤，T 装卸盾，X 卸主手；1–4 选法术，F 施放，E/Q 交互。F5 保存、F9 读档。

原实验台仍可用 **UE 5.8** 打开 `AetherLab.uproject` 后点击 Play。`1–5` 选择刺激，鼠标点击物体；`Tab` 执行组合场景；`R/G` 切换雨/风；`Backspace` 重置。

```powershell
.\Scripts\Build.ps1
.\Scripts\Test.ps1
.\Scripts\Smoke.ps1
.\Scripts\SmokeAdventure.ps1
.\Scripts\SmokeAdventure.ps1 -Art
.\Scripts\SmokeEquipment.ps1
.\Scripts\TestNetwork.ps1
.\Scripts\PlayAdventure.ps1
```

反应插件包含固定步长、空间查询、睡眠、燃烧/灭火、潜热相变、导电分配、结构和密闭爆裂代理。新增水面为显式连接的格子代理；破坏为预制刚体碎块。联网已验证服务器扣费与晚加入基线，尚未验收多人战斗、丢包和大规模场景。

`test/` 是另一个已有 UE 工程，本原型没有修改它。
