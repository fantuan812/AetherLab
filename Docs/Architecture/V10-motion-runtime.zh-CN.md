# v10 原生动作与作者源码
本单元未编译、未测试；没有生成新 uasset，也没有把独立 CPU 历史结果计为 UE 验收。

- AetherMotionRuntime 使用固定版本 C ABI 和逐文件大小/SHA-256/导出符号/ABI 检查。目录出现额外动态库时拒绝加载；模型、风格、原生库和许可证通过 NonUFS 制品清单部署。
- 进程共享一个原生串行 worker、一个模型。每角色最多一个在途任务和一个最新待处理意图；世界/Pawn/agent/动作/移动/配置/请求版本过滤迟到结果。GT 不等待推理；退出时等待安全归还 handle。
- 角色运动仍由 CharacterMovement 决定。30 FPS 输出用浮点帧重采样，实际消费四帧用作下一次上下文；隐藏 source AnimNode 使用不可变姿态，IK Retarget 接到目标人物原有动作层之前。
- 受击、腾空、格挡、攻击、施法、搬运、救援与传送期间由玩法禁止生成覆盖。缺资源/超时/错误时回到已有动画；连续有效结果后淡入。
- MotionAuthor.py 从锁定模型 API 提取骨架、真实 agent 烘焙、按 G1 FK 转换 GGUF 风格，并调用原生 loader 校验产物。源动画、骨架、版本、单位和哈希随转换记录保存。
- AetherMotionEditor 提供真实蒙皮 LOD 源网格、Manny/Quinn 双向 IK 链/重定向/Profile，以及 30 FPS G1 动画轨道导入导出；PrepareMotionAssets.py 接资源制作与 Cook 标签。

后续必须继续：制作与校准资源、蹲姿及全部受控动画、静态/无状态边界衔接生产调用、设置与经实测的自动后端策略、Vulkan/CPU 集成测量、双人物足部与握持质量、完整故障及专服发布验证。默认仍是传统动画，不宣称生成路径已经达到正式发布要求。
