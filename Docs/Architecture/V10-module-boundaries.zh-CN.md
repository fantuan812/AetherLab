# v10 模块边界实施记录

第一步将纯规则、规则扩展和世界定义移入 AetherCore，只依赖 Core 和私有 Json。旧平铺头保留转发，避免迁移过程中让调用方同时改名。Frontier 的角色、道具、世界状态、存档、模式和 HUD 声明分为独立头；这些反射类型仍属于 AetherLab，脚本路径不变。

验证：UE 5.8 Editor Development 增量构建通过；Aether.V10.Baseline、Aether.V9、Aether.V802 共 12 个自动化测试通过，包含冻结的 19 个旧档案回读。

尚未完成：Gameplay/UI/Editor 的完整依赖隔离、角色职责转移和迁移后的全部发布门禁。此记录不表示 V10-02 已验收。

第二步将地图作者工具迁入 AetherEditor，外壳定义共享到 Core。Runtime 不再引用 UnrealEd。初次报告实际为旧档用例通过、作者工具路径用例失败；先前根据进程退出码写成两项通过是错误记录，已在 UI 验证阶段纠正。启动验证发现 PreDefault 会提前加载游戏 CDO 与动画资源，改为 Default 后短启动与测试通过。

原生补充验证：锁定 CPU 模型的上游 inference-model 用例实际通过（1.74 秒），覆盖输入适配、输出帧/骨骼、确定性和所有权；这不是 UE 端帧预算测量。可用 BuildNativeMotion.ps1 -TestInference 重现。

UI 迁移前置：本地 PlayerController 经 Core 表现工厂选择 HUD，HUD 退出主动移除 Widget；Guide 调用方显式包含头，公开查询类型增加导出，Smoke 实现归入 Tests。增量编译通过，CheckV9 Run/Reload 通过。实际 UI 文件尚未迁移，旧 UMG 依赖暂留。

第三步在独立工作副本迁移 AetherUI。运行时不再编译依赖 UMG/Slate，专服目标排除 UI 模块。真实 HUD 工厂与退出清理保持不变，加入三个 UI 类精确重定向。非 Unity Editor 构建通过。

纠正后的严格验证：Scripts/Validate/TestRules.ps1 检查新报告的 failed/notRun/inProcess，不仅依赖 UE 进程退出码；14 项规则测试实际全部通过。UE 5.8 的 TryLoadClass 是直接 LoadClass，迁移工具需先 FixupCoreRedirects；测试明确断言此修正和目标类型，覆盖旧作者工具及三个 UI 类路径。普通库存与旧档规则另行保持通过。

真实地图 CheckV9 Run/Reload 通过，含 local_hud_factory。双客户端专服短测 AetherV807_0be1797b90d3 通过：专服没有加载 AetherUI，五次客户端连接含重连/服务器重启均有本地 HUD 通过记录。验证源码指纹 095685a770b4338f409138345ba9545db315ef7a59709fb37cfcdbea14afce90；此为 Editor 进程短测，不是独立 Shipping/四人发布验收。
