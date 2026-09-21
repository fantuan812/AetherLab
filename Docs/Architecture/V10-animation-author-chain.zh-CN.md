# 动画作者与真实推理验证入口
人物定义新增预载 AnimClass，G1 配置新增 source AnimBP；正式路径使用制作出的 ABP_AetherCharacter / ABP_G1MotionSource，原生图代理继续负责姿态评估。缺蓝图明确报错，不把 fallback 当内容完成。

ExportCrouchStyles.py 使用 UE 5.8 RunBatchRetarget 把官方动作派生的蹲待机/蹲走重定向到 G1，再按严格骨长、单位、30 FPS、源 uasset SHA256 导出。MotionAuthor.py 将其转换为 crouch_idle/crouch.mbstyle。StageMotionRuntime 只纳入来源和制品哈希匹配的自制风格；MotionAssets 将边界/风格接到两个人物配置。蹲姿兼容动画层只有在实际生成权重上升时才让出姿态，规则响应不等待推理。

新增 UE 内真实 CPU/Vulkan latent 用例，调用正式共享调度器、发布清单、模型、agent 和 infer，覆盖两个 agent、方向/风格切换、四帧边界、UE 34 骨姿态采样和释放后迟到结果隔离。此入口只覆盖集成正确性，输出实际耗时，不宣称通过视觉或性能门槛。

当前未执行作者脚本或测试，未编译、未测试；待统一阶段补资源、实测与修复。

## 统一验证修复

正式蓝图必须连接实际输出节点，不能依靠空 AnimBP 调用原生 GetCustomRootNode。人物节点转发初始化、骨骼缓存、更新、求值到原生代理图；G1 直接使用 GeneratedPose 输出。未 Cook 游戏需要加载的节点放入 UncookedOnly 模块，Shipping 包剥离编辑器依赖。

G1 原模型骨盆为坐标原点，参考 retarget pose 需把最低足部放到地面，以避免 UE 按零骨盆高度计算夸大比例。该校准不修改34骨定义和单位；正向地面root与反向骨盆root使用不同操作。作者可重复执行，派生蹲姿片段可按最新JSON重导入。实际回归证据见统一验证文档；本章早期“未执行”段落仅为历史记录。
