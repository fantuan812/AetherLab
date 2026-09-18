# AetherLab · UE 基础素材版

UE 5.8 C++ 开放世界灰盒。人物使用官方 Manny，场景和六种装备使用 Engine BasicShapes。旧试验台及 SwordMagic 美术已移除；旧工具与历史文档保留供追溯，不再作为当前入口。

## 运行

```powershell
.\Scripts\Build.ps1
.\Scripts\PlayFrontier.ps1
```

主机：`PlayFrontier.ps1 -Listen -Profile Alpha`；客户端：`PlayFrontier.ps1 -Connect 127.0.0.1:7777 -Profile Beta`。开发身份最多四个真人/AI 席位。默认打开工程后 Play 同样进入新模式。地图为 `/Game/AetherCore/Maps/L_Frontier`；需要重建基础地图时可运行 `Scripts/BuildPartitionMap.ps1`，已有静态场景修改会保留。

## 本轮代码

- 固定步热/水/燃烧/相变/压力；雨与电学水分离；有限电源、物理接触电传播、唯一接收端与完整电能分账。
- 搬运/推/投掷、支撑断裂落桥、限力铰链门、木箱浮力、落物伤害去重、占位冻结保护。
- GAS 近战/四元素/救援；背包实例和装备事务；八条主线、三类日常、商店买卖、离线及满包待领奖励。
- 两名唯一 AI 同行者、队伍邀请/接受/离队、修道院多阶段遭遇、三波公共活动、五类敌人行为、视线记忆与回巢；野外刷新冷却和公共掉落原子认领。
- 原生 Manny 动画图、跳落混合、攻击蒙太奇与基础足部 IK；中文主线追踪、地图目标和统一交互提示。
- Enhanced Input、菜单键位设置与冲突交换；UMG 功能面板；双世代校验存档、天气和区域降频；World Partition 地图与按需动态 NavMesh。

这些是灰盒实现，完整设计的全部验收尚未执行。[实现范围与限制](Docs/Implementation-v5.zh-CN.md)明确区分代码完成和未验收事项。[完整设计稿](Docs/Design-v4.zh-CN.md)是需求参考。

## 主要操作

WASD 移动，Shift 冲刺，Ctrl 跳跃，Space 闪避；左键轻击/按住重击，右键格挡。1–4 选元素，鼠标中键施法，F 锁定；E 交互，G 搬运/放下，C 投掷，V 推物。Q 生命药、Z 法力药；R/T 装备。I/J/K/M/P/Esc 打开背包/任务/技能/地图/队伍/菜单。F5 保存、倒地后 F8 回据点；开发模式 F10 反应调试、F11 晴雨。

轻量验证：`Scripts/TestLight.ps1` 仅运行 9 个小型规则测试；本轮不跑 `TestFrontierNetwork.ps1`、规模压力测试和旧模式回归。如需短启动/重启检查，运行 `Scripts/CheckFrontier.ps1`，使用独立测试存档。完整报告见 [Verification-v5.json](Docs/Verification-v5.json)。

后续玩法记录见 [Gameplay-v6.zh-CN.md](Docs/Gameplay-v6.zh-CN.md)。J 面板中 Tab / 下一项切换任务，M 查看同一目标的位置。`Scripts/CheckAnimation.ps1` 和 `Scripts/CheckGuidance.ps1` 各进行一次隔离存档的短启动检查，无需多人压力或全量 Cook；结果见 [Verification-v6.json](Docs/Verification-v6.json)。

反应与物理后续：[v7 实现记录](Docs/Reactions-v7.zh-CN.md)。新增有限水桶的原子水量/焓转移、独立剑刃切割、限时落物归属及材料签名兼容。`Scripts/TestReactionLight.ps1` 运行 8 个规则小测试；`Scripts/CheckReactions.ps1` 运行一次短场景检查。
