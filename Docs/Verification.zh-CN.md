# 验证记录

验证日期：2026-09-12。测试对象：本目录 `AetherLab.uproject` 和 `Plugins/ReactiveWorld`。

## 实际环境

- Unreal Engine **5.8.0**，Changelist **55116800**，本机 Epic Launcher 安装。
- Windows 11；AMD Ryzen 7 7800X3D；约 32 GB 内存。
- UBT 实际选择 Visual Studio 2022 的 MSVC **14.44.35211** 工具链与 Windows SDK **10.0.22621.0**。
- 目标：`AetherLabEditor Win64 Development`。

## 编译

**通过。** UHT 生成反射代码，UnrealBuildTool 编译并链接以下模块：

```text
UnrealEditor-ReactiveCore.dll
UnrealEditor-ReactiveRuntime.dll
UnrealEditor-AetherLab.dll
```

最终构建结果为 `Result: Succeeded`。实际输出分别位于插件和工程的 `Binaries/Win64`。

## 核心自动化测试

命令入口：`Scripts/Test.ps1`。0.3 的 UE Automation 报告：**20 成功 / 0 失败 / 0 未运行**，包括原有 15 项和新增 5 项。测试在引擎中实际执行。

| 测试 | 验证性质 | 结果 |
|---|---|---|
| AreaBudgetAndValidation | 范围能量均分；拒绝 NaN、负水量与无效参数 | 通过 |
| BoundedQueuesAndElectricalBudget | 注册/输入/传播预算；截断时余能保留 | 通过 |
| BurnExtinguish | 起燃、水混合降温、灭火事件 | 通过 |
| BurningWoodSpreadsAndCollapses | 邻居自然起燃，燃料消耗导致结构损坏 | 通过 |
| ContactAndOcclusion | 遮挡阻止传播，空气间隙不形成接触电路 | 通过 |
| ElectricalCycleConservation | 循环图单次访问，分支沉积能量总和守恒 | 通过 |
| FrozenBrittlenessAndBreakOnce | 冻结降低冲量阈值，破坏事件不重复 | 通过 |
| LatentHeat | 0°C 潜热平台，少量降温不瞬间冻结全部水 | 通过 |
| OpenFuelVersusSealedBurst | 敞口燃烧与密闭爆裂分离 | 通过 |
| SleepWakeWeather | 稳定睡眠，输入和天气变化唤醒 | 通过 |
| SpatialMoveRemoval | 负坐标格子、位移、注销与旧句柄安全 | 通过 |
| SteamEnergyMassBalance | 汽化水量、温度平台与蒸汽焓去向 | 通过 |
| ThermalConservationAndStability | 双向热流守恒，极大耦合系数仍有界 | 通过 |
| WaterAndMetalConduction | 积水连接导体，玩家使用相同规则 | 通过 |
| WindBiasesHeatTransport | 顺风侧比逆风侧升温更快 | 通过 |
| ConductionPreservesAttribution | 导电链保留最初施法者且电能仍守恒 | 通过 |
| LiquidTransferConservesMassAndEnthalpy | 液体转移守恒；遮挡/冻结/负输入拒绝 | 通过 |
| RestoreIsAtomicAndClearsTransientInput | 非法存档整批拒绝；恢复清除临时输入；相态重建 | 通过 |
| WeatherChangesProjectileEnergyAndTrajectory | 雨衰减热量，横风偏转，能量下限 | 通过 |
| Scale4096Bodies64Active | 稳定主体睡眠；局部唤醒；记录求解耗时 | 通过 |

机器可读原始报告：`Saved/Automation/Reactive/index.json`。UE 日志：`Saved/Logs/ReactiveTests.log`。可用相同脚本复现。

启动阶段日志曾出现引擎测试适配器的 `Condition failed` 信息，发生在本项目测试开始前；本表按项目 Automation 报告逐项判定，不将整份编辑器启动日志称为“零错误”。原实验台和修道院游戏运行日志另行检查。

## B–E 修道院集成验证

`Scripts/SmokeAdventure.ps1` 在实际 GameMode 中通过 **26 项检查**：不足魔力拒绝、GAS 扣费一次、目标冻结、冰面 Pawn 碰撞、完整存档、重击断绳、桥路开启、雷击人物、伤害来源、远距离交互拒绝、救援证词、读取记录、解誓、古印留存、返回奖励防重复、恢复任务/结构/Boss/冰态、纯剑击击败 Boss、携走古印结局和有限取水等。

成功标记：`AETHER_BCDE_SMOKE_PASS failures=0`。脚本化检查会安排角色位置与状态，它证明系统连通性，不等于真人从入口打完整场战斗或已经验证关卡乐趣。

## 服务器与晚加入验证

`Scripts/TestNetwork.ps1` 使用本机专用服务器和两个依次加入的客户端。两次都收到 `AETHER_NET_CLIENT_PASS`，检查：

- 服务器预先冻结的水面在客户端恢复冰比例与 Pawn 阻挡。
- GameState 的誓约记录标记作为基线到达。
- 拥有角色的客户端请求 GAS 火球，服务器扣费后的魔力复制回来。
- 客户端直接提交物理输入被拒绝；本地权威求解器注册主体数为 0。

第二个客户端在第一个退出后进入，因此实际覆盖晚加入。没有以此宣称两人同时战斗、重连恢复、高延迟、丢包、带宽上限或跨相关性区域测试已经通过。

## 稀疏数值规模基准

场景为 **4096 个主体、64 个活跃主体、100 个测量步骤**，主体间隔 1000 cm、半径 10 cm。一次本机记录 P50 **0.0647 ms**、P95 **0.0875 ms**，预算截断 0 次。每次运行在日志输出 `AETHER_BENCHMARK`，机器状态会影响数值。

这是纯 CPU 核心稀疏场景，主体间几乎不交换热量；不含渲染、UE 遮挡射线、网络、角色 AI 和 Chaos。不能据此估算密集燃烧城市或宣称达到整帧性能预算。

## UE 场景集成测试

命令入口：`Scripts/Smoke.ps1`。以 `UnrealEditor-Cmd -game -nullrhi` 运行真实 GameMode 与 WorldSubsystem。

**通过，进程退出码 0，最终运行日志没有 `Log*: Error:`。**

断言包括：13 个样本创建/注册；木头起燃；水冻结；密闭燃料爆裂；两个敌人与玩家样本都收到 Shock 回调；累计电能沉积达到本次输入的 5 kJ。

最终成功日志：

```text
AETHER_SMOKE_PASS | Bodies 13 | Active 13 | Heat pairs 8 |
Electric visits 0 | Step 29 | Rejected 0 | Budget 0 | Dropped 0.022s
```

其中 `Electric visits` 是最后一步计数；电脉冲已在较早一步完成。触电检查使用样本的累计回调计数与能量累计值，不用最后一步访问数推测是否导电。

测试过程验证了普通 Static Mesh 的刚体启用与事件桥执行。尚未使用制作好的 Geometry Collection 验证具体碎裂形态；那部分 API 已通过编译。普通道具没有 ASC 时会跳过 GAS 发送，仍收到组件事件，避免把未接入 GAS 的道具当作错误。

Unreal 启动时存在其自带编辑器模块的 widget-factory 警告。它们不等于项目运行错误。首次受限沙箱运行曾因无法写入 Zen/DDC 缓存而失败；随后在获准访问本机 Unreal 缓存的环境中运行成功。

## 渲染检查

0.3 修道院由 `Scripts/CaptureAdventure.ps1` 使用真实 UE 渲染导出 **1600×1000** 截图，见 `Docs/Images/AetherAdventure.png`。已经检查角色/剑盾、水面色彩、灰盒通路、天空照明、瞄准提示和 HUD。该画面展示基础形体，尚无正式动作动画和完成的幻想建筑美术。

已用 UE 5.8 的真实渲染路径在后台运行实验场，导出并人工检查 `Docs/Images/AetherLab.png`：场景样本、材质颜色、固定相机与 HUD 正常显示。截图是基础调试视图；详细数值通过鼠标指向样本后的 HUD 查看。

运行参数为 `-game -AetherCapture -RenderOffscreen`，截图模式会自动退出，不留下后台游戏进程。该检查不是鼠标/键盘交互自动化测试，也不是正式美术验收。

## 验证范围

这些结果证明数值规则、UE 生命周期和基本反应链在当前引擎构建上运行。没有据此宣称完成以下验收：

- Shipping 打包或其他平台构建。
- 客户端施法预测、动作动画/打断和玩家完整操作流程。
- 多人同时战斗、丢包、高延迟或 Chaos 网络重模拟。
- 任意地形连续水流、真实气体输运、支撑图建筑坍塌和正式 Geometry Collection 资产。
- 几万对象的大世界性能、长期运行数值漂移。
- 正式 Niagara 美术、音效或资产化关卡。

架构文档分别给出了这些部分的扩展边界和开发顺序。

## 0.4 模块化角色与装备更新（2026-09-12）

最终 `Scripts/Verify.ps1` 整体通过，记录见 [ModularVerification.json](ModularVerification.json)。UE 5.8 Development 编译成功；20 项自动化测试、34 项装备集成检查、旧/新美术关卡各 26 项检查均通过。新关卡使用实际导入的 Blender 身体、独立剑盾/锤、36 个玩法对象与原型动画。

本机专用服务器和两个先后加入的客户端通过装备切换 RPC、远端角色装备附件、晚加入基线、冻结碰撞、GAS 扣费与客户端权威拒绝检查。没有进行丢包/高延迟和多人同时战斗压力测试。

真实 UE 渲染已检查身体颜色、剑盾与锤的独立挂接尺寸、换装后的身体保留以及修道院布局，截图在 `Docs/Images/SwordMagic_*.png`。修复了骨架根单位缩放引起的过大武器、骨骼材质未持久保存、桥柱与可断绳重叠以及学徒与回廊柱重叠的问题。

本更新实现了受击/架势打断的装备攻击取消及原型动画；正式 AnimBP、双手 IK、精确刀刃扫掠和 Shipping 包仍不在已验收范围。下次扩展时按 [模块化玩法契约](ModularGameplay.zh-CN.md) 添加数据或行为实现，并执行相应回归。
