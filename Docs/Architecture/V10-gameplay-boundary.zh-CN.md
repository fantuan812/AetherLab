# Gameplay 编译边界和职责收口
AetherLab 现在只保留主模块与独立反应实验室样例装配。角色、库存、世界、动作、技能、交互、持久化和网络接口迁入 AetherGameplay 的 Public/Private 功能目录；UI 和 Editor 直接依赖 Gameplay，不再反向依赖装配根。仍使用非 Unity 编译。专服不引入 AetherUI/CommonUI。

本次不仅调整目录：现场服务分为训练、社交、机关、遭遇处理器，注册表同时决定查询可用性和执行路由；GameMode 入口只做权限、距离、版本、目标复验和上下文装配。动画更新改为一份身体快照、移动状态、动作时间轴、接触 IK、独立图代理，图代理负责传统和生成姿态混合。此前输入、战斗、世界动作已各有实际状态所有者。

逐类型迁移清单见 V10-gameplay-type-migration.json。Windows/Linux 和各自 Server 的引擎配置加入精确 Class/Struct/Enum Redirect；没有包级通配重定向，避免覆盖先前 UI/Editor 迁移。旧 v9 reader 仍接受原脚本类名，也允许同格式调试存档的新类名，序列化字段顺序没有改变。

验证状态：已核对源码路径和导出宏，新增旧类解析回归用例但未运行；未编译、未测试。资产重存、非 Unity 构建、旧档夹具、专服与 UI 验证进入统一阶段。
