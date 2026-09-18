# v10 事务存储边界

当前实现是新后端与测试基础，尚未替换游戏中的 v9 保存入口。旧档 dry-run、备份迁移工具、类型化玩法 Gateway、角色属性投递消费者与所有领域接入仍待实现，不能将后端规则测试视为整套游戏验收。

## 单写者与提交

AetherCore 定义值对象契约，AetherGameplay 实现 SQLite 写线程。64 个待处理任务上限（可配置 1–256）、32 个聚合写上限、每条聚合 4 MiB、总载荷 8 MiB；不让后台捕获 Actor/UObject。角色、世界和容器聚合在同一 BEGIN IMMEDIATE 中校验版本并提交；协调者得到成功结果后才能回游戏线程发布状态。读取失败、缺失和未知 schema 是不同结果。

SQLite 固定 3.53.4，自带官方源码和符号前缀，避免误用 UE 5.8 的 3.47.1。官方包 SHA3-256 已核验；源码锁和校验脚本在 Build/ThirdParty、Scripts/Validate。启用原生 OS VFS、WAL 与 synchronous=FULL，并在打开时核实配置。关闭停止接单、排空队列，由连接所有者线程关闭数据库。Close 必须由生命周期所有者执行，不得在写线程 Future 续延中等待自己。

## 回执和效果

每个改变资产的请求都推进提交者角色聚合版本。最近 64 条回执持久保存完整有界请求与结果；相同 ID 不同请求拒绝。命令 ID 高 64 位编码期望版本加一，低 64 位由随机 GUID 提供，创建统一使用 NewCommandId；旧 ID 因绑定原版本不能在淘汰后配新版本重用。此规则由存储独立验证，不能只靠按钮禁用。

效果投递随事务入库，每个角色最多 128 条待投递。效果 ID 同样绑定版本，确认按角色与 ID 删除；消费者仍需根据持久化目标属性版本幂等恢复，当前尚未接入 ASC。后端拥有投递记录不等于游戏已经解决消耗品崩溃窗口。

在线备份调用 SQLite backup API，不复制活跃 .db 文件。未知 application_id/schema、完整性检查失败均拒绝打开；保留原文件，不重建空库。该保证不覆盖硬件损坏或断电测试。

## 已执行

Editor 增量构建及 17 项规则测试通过（包含 3 项真实 SQLite 文件用例）：跨聚合回滚、响应丢失后重试、效果重启恢复、双连接并发、物理回执上限、旧 ID 版本绑定、关闭排空、在线备份及未知 schema 原文件保护。进程强制退出恢复测试另行执行并记录，不与普通异常返回混为一谈。

官方依据：[WAL 与 WAL-reset 修复](https://www.sqlite.org/wal.html)、[3.53.4 发布记录](https://www.sqlite.org/releaselog/3_53_4.html)、[在线备份 API](https://www.sqlite.org/backup.html)。

进程恢复补充：TestStoreCrash.ps1 的 Seed、CrashBefore（退出 91）、VerifyBefore、CrashAfter（退出 92）、VerifyAfter 五阶段全部通过，报告目录 e850ab7b61554445acbd0d9586ae6230。COMMIT 前强退后两个聚合均为旧版且无效果；COMMIT 后强退恢复新版、唯一效果及回执。最终入队前边界校验增量编译与 17 项规则重跑通过，报告 Rules-f815eaae361745b1b4bf7fd9b7014c64。没有执行断电、硬件损坏或正式个人档迁移。
