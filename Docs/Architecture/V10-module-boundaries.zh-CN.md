# v10 模块边界实施记录

第一步将纯规则、规则扩展和世界定义移入 AetherCore，只依赖 Core 和私有 Json。旧平铺头保留转发，避免迁移过程中让调用方同时改名。Frontier 的角色、道具、世界状态、存档、模式和 HUD 声明分为独立头；这些反射类型仍属于 AetherLab，脚本路径不变。

验证：UE 5.8 Editor Development 增量构建通过；Aether.V10.Baseline、Aether.V9、Aether.V802 共 12 个自动化测试通过，包含冻结的 19 个旧档案回读。

尚未完成：Gameplay/UI/Editor 的完整依赖隔离、角色职责转移和迁移后的全部发布门禁。此记录不表示 V10-02 已验收。
