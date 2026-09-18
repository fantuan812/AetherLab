# v10 世界 DTO 与完整旧档离线导入

## 当前范围

世界 DTO、全快照转换及离线原子导入已实现。游戏启动/保存入口仍使用原 v9 后端；工具不激活新库，也不代表地图恢复或 v10 全部功能验收。

## 世界数据与编码

FAetherWorldStateV10 显式保存世界版本、来源版本/代次/SHA256、环境与世界开关、反应体、掉落、营地回执、旧服务回执、世界事实来源，以及 Abbey/Relay 遭遇状态。

反应体保留 StableId/RegionId、Transform、机关/支撑/能量源状态、材质签名/格式与所有权威物理标量。温度、冰比例等派生值不另存一份。旧掉落保留两种表达（Items 或 Definition/Count），尤其保留 ClaimId 和 ClaimedBy；它还不是新实例掉落容器。

AWLD schema 1 使用显式小端整数、IEEE 浮点位模式和有界无损 UTF-8；每行最多 4 MiB。4096 反应体、128 掉落、32 营地、64 旧服务回执和 512 世界事实均在分配前限制。截断、尾随、未知格式/标志、NaN、重复身份、非法引用或物理范围会失败，输出保持原值。四元数必须已归一化，读取器不会修复损坏输入。

旧服务回执 ExpectedRevision 必须小于对应角色版本。遭遇波次 0–3，锁定席位 2–4；参与者是累计列表，可以超过四人，结算列表必须是其无重复子集。保留原 PhaseStarted，跨启动恢复仍需要时钟重基准和有限遭遇恢复策略。

## 全快照转换

ConvertSnapshot 先转换全部角色，建立角色版本索引，再转换世界。只有全部成功，才发布最多 128 角色加 World/Main 的 FAetherLegacyImport。版本不归零；schema 4 的物理记录也不被清空。旧反射字段和默认值保持冻结。

旧 FName 定义引用以不区分大小写的唯一对应转成规范字符串。未知物品、任务、事实来源或不兼容容量返回诊断，不删除旧记录。后台存储只接收值对象，不持有旧 UObject。

## 工具

关闭正在写入来源的游戏，在项目根执行：

```powershell
# 完整转换预检，只写来源备份与报告
./Scripts/Migration/AuditLegacy.ps1 -SourcePath '完整路径/Profiles.sav'

# 在本次独立备份目录内创建 SQLite，关闭重开并核对全部行
./Scripts/Migration/AuditLegacy.ps1 -SourcePath '完整路径/Profiles.sav' -Import
```

每次运行位于 Saved/V10Migration/<GUID>，原始 sav/侧车保留；schema 5 必须匹配提交代次和 CRC。只有精确的冻结合成夹具允许 -Fixture。导入只能创建同目录 state.sqlite，不能指定生产覆盖目标。来源校验或任一转换失败时不打开数据库。

报告区分 finalSchemaConverted（完整领域 DTO）、databaseWritten（已提交）、importVerified（关闭重开后所有版本/负载及幂等标识复验）与 activated（始终 false）。只有来源未变化且全部检查通过才报告工具成功。事务与标识规则见 [原子导入](V10-atomic-import.zh-CN.md)。

## 验证与剩余工作

验证仅使用合成档案。覆盖全部世界字段、已领取掉落、所有截断点、NaN/数量攻击、跨角色引用、遭遇结算、schema 4 保留物理记录，以及 19 角色+世界的真实 SQLite 回滚、提交、重开和重复导入。

仍需完成：根据实际地图与材质复验并恢复、跨启动遭遇时钟/实体恢复策略、正式保存入口切换、鉴权网关和 owner-only 快照、异步提交后的会话/Pawn epoch 校验。离线转换成功不能替代这些运行时验收。

实际结果：UE 5.8 Editor Win64 Development 增量编译通过；42 项规则全部通过（Rules-e5742bbbdcf14636a63723e63c56680e）。三进程离线工具测试通过（V10LegacyAudit/9e1ae09c53d444abb1c3ad0fb02d4452），覆盖只读审计、导入后重开、错误校验和在建库前拒绝、来源及三份独立备份字节一致。未读取个人存档。
