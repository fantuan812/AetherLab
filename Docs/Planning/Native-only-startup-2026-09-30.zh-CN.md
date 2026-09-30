# 原生存档单一路径：自动旧格式导入拆除

本批基于动作契约提交 bcfe40e；属于兼容层清理的第二批，不宣称所有旧 DTO 投影已拆完。

## 已实现

- 删除运行时自动 .sav/.crc 读取、备份、转换、旧格式解析器、Profile/World 转换器、ImportLegacy 事务存储 API 和 SQLite 导入实现
- 删除对应不再受支持的旧格式迁移命令行/审计入口及仅针对该特性的测试。保留历史 Docs/Fixtures 与历史验证记录，不删除或转换任何用户存档
- WorldBootstrap 只接受当前原生数据库或明确授权的空命名空间初始化；没有导入参数、格式猜测或旧档 fallback
- 原生库没有世界行且存在相同命名空间的历史 .sav 或 .crc 时，返回 AETHER_SAVE_FORMAT_UNSUPPORTED，绝不悄悄生成零进度世界。已有角色/容器但无世界行返回 AETHER_SAVE_WORLD_MISSING
- 现有合法原生世界仍优先；历史原件留在磁盘不影响其加载。当前库本身依旧执行 schema、数据和全库引用审计，不因存在旧档而降级
- AetherLegacyRuntime/AetherV4Smoke 旧执行模式显式拒绝，不能切换到旧 writer；普通事务崩溃恢复探针仍保留，旧 Import 模式拒绝
- 删除两个过渡头 Framework/AetherRules.h、World/AetherWorldDefinition.h，所有调用直接包含 AetherCore 当前定义

## 恢复边界

此分支没有新增离线旧档转换器，不会自动迁移、覆盖、删除或重置旧存档。格式不支持时可由用户恢复匹配当前 schema 的原生备份；想重新开始必须明确使用另一个存档名称。不能把旧档移动走作为无提示清零手段。打开 SQLite 可能建立一个空数据库文件；在历史文件阻断下不会写入新世界或角色进度。

原生 schema 内已有的 LegacySourceSha256/来源回执等是现存档的审计数据，不是可执行兼容适配器。本批保留这些字段和现有序列化布局，避免无授权改变用户已有原生存档格式。

## 未完成/未验收

- 旧 PlayerState.Profile 与 GameMode.Database 场景投影、LegacyBit 快捷键映射、非当前物理格式读取仍需逐域替换；本批不把这些改名后当清理完成
- 旧流程测试在历史分支上保留，本分支统一验收使用当前 schema 的 native journey/network/checkpoint 测试，不继续运行已退役的旧格式模式
- 新增 Aether.Systems.Persistence.CurrentSchemaStartup 纯策略回归，覆盖旧档阻断/孤立记录/授权/命名空间/原生优先；未编译、未测试
- 待统一验证：当前原生库重启与重连不回退；只有旧 .sav、只有 .crc、空 SQLite+旧档均明确失败并保持源哈希；未知当前 schema 失败；合法空命名空间可建档；当前跨聚合提交的崩溃/重试仍保持原子性
- 本批只做源代码依赖检查与 diff 空白检查，不把静态检查称为 UE 编译、功能或磁盘保全实测

## 独立审查修复

- 退役依赖 AetherV4Smoke 的 Scripts/SmokeFrontier.ps1，不留下必失败的当前入口，也不偷偷转发成长时间旅程测试
- 未知数据库/聚合/索引版本统一明确返回 AETHER_SAVE_SCHEMA_UNSUPPORTED，附当前/期望版本并保留原数据；新增内存 SQLite 行/索引回归用例（未运行）
- 显式无效、空或超长 AetherSavePrefix 直接拒绝，不忽略参数进入默认存档；入口与策略统一64字符边界，新增不改变目标命名空间的回归
- 移除旧迁移SHA256删除后不再使用的 Gameplay/Editor OpenSSL 依赖

行 schema 在 SQLite 原始 int64 上检查后再缩窄，避免超大版本截断伪装当前版本；新增4294967306拒绝回归（未运行）。
