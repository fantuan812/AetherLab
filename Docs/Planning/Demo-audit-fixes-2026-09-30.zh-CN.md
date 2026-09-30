# Demo 审查修复 · 2026-09-30

基线：`17c1b290281a64fe3c08a219f6f43d63a5687692`。本次只修改代码、回归和入口文档，不修改美术、地图、个人存档或水术溢出政策。

## 修复内容

1. 生产 `ResolveNativeContext` 使用共享 `FAetherDropSlots` 解析器。启动恢复保留已审计的非活动掉落描述符，提交发布更新槽状态；新 Drop 优先复用非活动墓碑，继续使用原修订 CAS，绝不覆盖活动物品。两个并发命令选同一槽时仍由事务版本裁决。旧修订的延迟发布不再重新激活已更新的掉落。
2. Drop 新行准入保守预留129个容器位置（当前128个档案及1个共享箱）。SQLite `BEGIN IMMEDIATE` 内再次检查整批新容器数量，防止快照与个人箱初始化交错后超量插入；失败回滚所有聚合，不写成功回执。已有墓碑复用不消耗新行。
3. 初始化达到4096上限返回独立 `Capacity`，不会与身份冲突/损坏混淆。场景仅延后该箱创建并给受影响用户提示，不再调用全场失败。已被旧版本填满的存档不会自动删除或迁移墓碑：个人仓储可能仍暂不可用，但玩家保持连接且已有掉落可复用。此补丁不修复已经超过4096行或已损坏的存档。
4. F5 原生模式请求 `SaveLoadedPhysics`，Future 完成后仅在 `Committed/Replayed` 且带世界结果时确认成功并显示修订。请求中的重复按键、Busy、失败均有独立反馈，允许稍后重试。旧模式保留旧保存流程。角色变更仍由各自事务保存，F5 不伪称冻结全世界所有未完成操作。
5. 电击窗口在入口捕获湿润度、各来源/控制器弱引用、各来源防御和事件时间；立即与屏障排队使用同一闭包结算。眩晕冷却按事件时间推进，已过期的历史眩晕不会在排空时取消新动作。
6. README 指向9月29日的实际验证及本次未验证边界；规则脚本默认纳入 `Aether.Systems.`。

## 新增/改进回归

- `Aether.V10.Commands.ContainerCoordinatorAuthorizationAndCompetition`：使用与生产现场相同的槽解析器，两个不同命令 GUID 的 Drop → PickUp → Drop 复用同一行，继续覆盖竞争及回执重放；不是完整地图/射线命中测试。
- `Aether.Systems.Persistence.ContainerCapacityInterleaving`：直接建立有界旧墓碑夹具，读快照 → 个人箱创建 → Drop提交；验证容量、原子回滚、无成功回执，并验证满4096行及旧修订发布。
- `Aether.Systems.Runtime.ContainerCapacityStaysLocal`：调用真实 `TickNativeContainers` 的创建结果排空，满额新用户箱不会使场景失败。该隔离场景测试不等于多人网络验收。
- `Aether.Systems.Runtime.ElectricalWindowSnapshot`：实际角色/资源屏障、改变湿润及抗性、两来源和相隔超过3秒的强电窗口，对比立即与排队伤害及逻辑时间。
- `Aether.Systems.Persistence.ManualSaveConfirmation`：生产 F5 Future 管理器的异步确认、重复请求、Committed/Replayed、Busy、失败、重试及缺失证明。
- `Aether.Systems.Persistence.ManualCheckpointReopen`：真实 SQLite 与检查点状态机、物理 DTO 捕获、修订确认、关库重开后的变换/水量恢复；不替代真实 F5 输入、场景物理恢复和重启可玩检查。

## 验证状态

- 实现阶段遵守仓库先完成代码再统一验证的顺序。
- 本次新增 UE C++ 与 UE 自动化用例：未编译、未运行。当前云执行环境未发现 UnrealEditor/UnrealBuildTool/RunUAT 或 PowerShell；不能把静态检查当 UE 编译通过。
- 9月29日已有验证仅对应其候选和当时配置，不能转用于此补丁。当前基线/本次变更的完整旅程、多人、动作质量、长时运行、Shipping/专服包仍未验收。
- 已运行并通过：`git diff --check`；SQLite锁定源码按UTF-8/LF归一化SHA256核对（2文件）；改动UHT头的generated.h最后包含顺序；改动文档本地链接检查。它们均为静态检查，不是C++编译或游戏测试。
- 发布状态以PR/提交记录为准；需要 UE 5.8 的后续检查见下。

## 最小后续验收

1. 在 UE 5.8 环境增量构建，再运行 `Scripts/Validate/TestRules.ps1`（默认已包含新增分组）。使用独立测试存档，不覆盖个人进度。
2. 实际地图 Drop → PickUp → Drop，确认相同槽、数量不增长；两客户端争抢同槽与满额旧档新用户进入，确认其他玩家不被退回菜单。
3. 移动反应物体后按 F5，等待确认修订，退出重启核对位置与水量；并覆盖存储 Busy 和失败后的重试。
4. 屏障期间电击再改变湿润/抗性，排空后核对伤害；强电间隔超过3秒及事件已过期的情况，核对冷却和动作取消。

仅提出补丁，不自动合并或部署。当前内容定义扩展若需要超过129个静态/个人容器，应同步调整准入预算；本补丁的预留量对应现有定义。
