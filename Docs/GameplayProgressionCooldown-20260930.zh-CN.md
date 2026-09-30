# 成长冷却门禁修复

基线：489c8f049ce0e6fcd58fc41326adc5ffa94102c2。仅源码审计；本轮不编译、不执行规则测试、不启动 UE。

## 已确认问题

- Core 的 FAetherSkillRuleContext.bCoolingDown 是角色级布尔值。AetherSkillState.cpp 的 Busy 同时阻止 CanLearnNext、LearnNext 与 Reset；没有按目标技能排除其他技能的语义。
- NativeContext 和 SkillTreePage 都将 bCoolingDown 赋成 bCasting，未读取已有的技能/组冷却截止时间。
- Skills.json 的 Body.Haste 有 25 秒 SkillCooldown、0.3 秒 RecoverySeconds，因此施法恢复结束后仍可能有真实冷却。
- PlayerState.CommitCooldown 已记录 SkillCooldowns，BuffRuntime.RefreshSnapshot 已将其投影为拥有者 Cooldowns 快照。不新增计时权威，也不声称洗点会刷新冷却；现有账本会保留冷却。

## 实现边界

1. 新增 Gameplay/Skills/AetherSkillProgressionContext 共用读取器，只填充 bCasting 与 bCoolingDown，保留调用方的战斗、等级、服务资格。
2. 服务器查询 PlayerState.SkillCooldowns；客户端查询已有 BuffRuntime 原子快照，并使用现有 PresentationReady(profile revision) 检查生命、版本及授权对齐。二者使用 CombatTime 的服务器时间口径，只看未到期截止时间。
3. NativeContext 与 SkillTreePage 使用同一读取器；技能树缓存键纳入冷却布尔状态，截止时间自然经过时无需档案版本变化即可刷新。
4. 添加回归源码覆盖冷却仍在但恢复结束、仅组冷却、已到期、无快照、三个成长命令拒绝且不产出事务、期限后恢复与账本不变。按 AGENTS 不执行这些测试。

独占文件：新建 Source/AetherGameplay/Public/Skills/AetherSkillProgressionContext.h、Private/Skills/AetherSkillProgressionContext.cpp、Private/Tests/AetherSkillProgressionCooldownTests.cpp；修改 AetherNativeContext.cpp、AetherSkillTreePage.cpp。不修改 Combat、PlayerState、BuffRuntime 或并行岗位文件。无旧协议转换或兼容层。

状态：实现已提交，待独立源码复审；未编译、未测试。不得据此声称 UE、多人或完整玩法验收通过。

## 本次改动

- 以共享 ResolveExecutionState 读取独立技能及共享组的任意未到期截止时间；三个成长命令继续复用 Core 既有 Busy 规则。
- 服务器只读 PlayerState 的权威截止时间；客户端只读既有 BuffRuntime 对齐快照。客户端缺服务器 GameState 时间、生命身份、档案/授权版本对齐时保持禁用。
- 技能树缓存键纳入 bCoolingDown，沿用现有页面刷新机制；不会等待档案版本变化，也不创建另一个冷却计时器。
- 添加三个 Automation 回归源码：权威源与独立恢复期、拥有者原子快照对齐、Learn/Upgrade/Reset 的拒绝与到期恢复。仅完成源码编写，未执行。
- 源码空白检查干净。未编译、未执行规则/自动化/UE/多人测试。未改存档、协议、GAS施法、Niagara、MotionBricks或兼容入口。
