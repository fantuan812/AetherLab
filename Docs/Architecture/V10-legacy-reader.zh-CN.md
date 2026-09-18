# v10 旧档读取与离线预检

当前阶段只完成旧档的识别、受限解码、旧规则校验、备份与预检报告。尚未转换固定格/技能/奖励 DTO，也没有写入或启用 SQLite；不能把本工具当作完整迁移成功。

## 使用

关闭会写入该存档的游戏实例，然后在项目根运行：

```powershell
./Scripts/Migration/AuditLegacy.ps1 -SourcePath '完整路径/角色档.sav'
```

每次运行建立独立的 `Saved/V10Migration/<GUID>`，保存原始字节备份、存在的 CRC 侧车、引擎日志、reader.json 和 manifest.json。源文件只读。CreateNew 和 NoReplaceExisting 禁止覆盖已有备份和报告。解码针对备份；源文件若在此期间改变，工具拒绝采用报告。报告/备份不进版本控制。

生产 schema 5 必须具有匹配 `Generation:CRC32` 的 .crc 侧车；只有冻结合成夹具的精确 SHA-256 可用 -Fixture 跳过侧车检查。SHA-256 使用引擎随附 OpenSSL，避免调用 Windows 上未实现的 GenericPlatform 接口。

## 格式边界

- 只接受 GVAS 3、当前冻结 UE 5.8 序列化版本，以及 /Script/AetherLab.AetherFrontierSave 类名；不根据文件内容加载类。
- v9 标签流具有顶层控制字节，嵌套结构没有。写入器省略等于默认值的属性，包括默认 Version=5；显式 Version=4 也保留读取分支。没有用更早引擎的二进制夹具声称跨引擎支持。
- 在 UE 分配集合之前，扫描完整类型树、集合数量、属性大小、字段唯一性、嵌套深度和对象尾部；超量计数、未知类型/扩展、截断或尾随内容均拒绝。
- 限制 16 MiB 文件、512 字符级序列化上限、128 角色、32 库存实例、64 回执和 4096 世界记录。第二遍只按经过预检的冻结反射布局读标签，不使用 Profile.NetSerialize/SerializeBin。
- 字段类型和 CDO/结构默认值共同形成指纹 `c95976b1682573e91fd1428684ee763e`。改变旧字段或默认值会关闭读取入口，要求明确维护版本化 reader，不能静默把新类型当旧类型。
- Content/AetherCore/Definitions/Legacy/V9Rules.json 与 v9 基线 01f9709 的规则相同，单独用于旧档验证。新物品和技能模型应定义新 DTO，不能直接在被冻结的 v9 字段上改变存盘语义。
- 角色、世界账本、世界标量先验证；材质签名与实际地图对象的对应关系仍需后续真实恢复验证。预检通过不等于最终 v10 不变量或地图恢复通过。
- 临时 UObject 由结果的 TStrongObjectPtr 持有，解码发生在游戏线程。后续迁移需转换为独立值 DTO，才可交给数据库线程。

## 验证

```powershell
./Scripts/Build.ps1
./Scripts/Validate/TestRules.ps1
./Scripts/Validate/TestLegacyAudit.ps1
```

规则用例覆盖冻结的 19 个合成角色、全部旧法术位、满包、双手引用、未领奖励、世界 Transform/掉落/回执、MAX_int32/负数/超量/错位数组计数、未知版本/类/控制扩展、截断、尾随字节、重复角色和非法世界标量。离线工具另用两个独立进程验证有效提交侧车通过、错误侧车拒绝、原文件未变、两份备份字节一致。

待完成：完整 schema 转换、已导入标记与全档原子事务、重复迁移与中断回滚、首次角色恢复以及游戏保存入口切换。当前游戏仍使用 v9 快照存储。

本工作单元实际结果：Editor 增量构建成功；18 项规则全部通过，报告 Rules-94c0364c33014b3298d9ea928bd0a867。离线提交校验/备份用例通过，报告 V10LegacyAudit/e244dd5678c04cbfbe8cb516e3f27c6a。均使用合成档案，没有读取个人存档或执行游戏保存入口切换。

后续进展：角色子对象现已接入只读转换与显式 DTO 编码，详见 [完整角色 DTO](V10-profile-dto.zh-CN.md)。审计同时报告 profilesConverted，仍不写数据库，也不把角色转换等同于世界/整档转换完成。
