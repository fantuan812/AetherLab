# v10 模块边界实施记录

第一步将纯规则、规则扩展和世界定义移入 AetherCore，只依赖 Core 和私有 Json。旧平铺头保留转发，避免迁移过程中让调用方同时改名。Frontier 的角色、道具、世界状态、存档、模式和 HUD 声明分为独立头；这些反射类型仍属于 AetherLab，脚本路径不变。

验证：UE 5.8 Editor Development 增量构建通过；Aether.V10.Baseline、Aether.V9、Aether.V802 共 12 个自动化测试通过，包含冻结的 19 个旧档案回读。

尚未完成：Gameplay/UI/Editor 的完整依赖隔离、角色职责转移和迁移后的全部发布门禁。此记录不表示 V10-02 已验收。

第二步将地图作者工具迁入 AetherEditor，外壳定义共享到 Core。Runtime 不再引用 UnrealEd。精确的旧作者工具类路径重定向、44 个静态几何定义和冻结旧档回读通过（2 个 v10 测试）。启动验证发现 PreDefault 会提前加载游戏 CDO 与动画资源，改为 Default 后短启动与测试通过。

原生补充验证：锁定 CPU 模型的上游 inference-model 用例实际通过（1.74 秒），覆盖输入适配、输出帧/骨骼、确定性和所有权；这不是 UE 端帧预算测量。可用 BuildNativeMotion.ps1 -TestInference 重现。
