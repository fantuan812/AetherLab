# v10 基线与实施约束

实施基线为 01f9709047edde1b7ed276cc0a738c196957c3c5，现有 main 与 origin/main 一致。用户要求执行完整方案并多写代码注释；实施规格归档于 [方案](../Planning/Implementation-v10.zh-CN.md)，完成情况单独记录于 [工作包状态](../Acceptance/V10-status.json)。

## 已冻结

- UE 5.8.0，CL 55116800；Windows x64，非 Unity Editor Development 增量构建。
- 本机 Ryzen 7 7800X3D、约 32GB RAM、GeForce RTX 4070 Ti，NVIDIA 驱动 32.0.15.9649。此为实际执行机，不代表未测试的最低硬件也已支持。
- MotionBricks ee0cf5d9035f639ed0787f390fb1ce05d6a4c463、GGML 8c63e70982c95ceb862e3a1073a2c1beef75d60a、模型修订 cc2a47603dbc203a4f18f35dd06ed3611833f506。版本锁与上游逐文件哈希位于 Build/ThirdParty。
- 19 个合成 v9 档案使用现有版本真正序列化；包含 16 种法术位组合、满包、双手装备与待领奖励。二进制夹具及 SHA256 在 Docs/Fixtures/V9，普通测试只读，生成参数显式防止覆盖。
- 用户开始任务时已有 DefaultEngine.ini 本机改动，保留在工作树，禁止把个人配置混入提交。

## 验证与待验收

夹具增量编译成功，1 个冻结/回读规则测试通过（内部检查 19 份档案与固定身份）。MotionBricks CPU 动态库与 CLI 已按真实 GGML 配置构建成功；模型及风格资源已完成下载并逐文件验证 SHA256，尚不声称推理、重定向或游戏集成完成。

方案中的 60 FPS、150ms 新计划 P95 等门槛保留原值。Vulkan、Linux 专服、完整 UI、四人网络、发布包和长时测试均待对应工作包执行，不使用本机 CPU 库构建成功替代。

## 注释约定

公开接口说明输入、返回和状态所有者；事务写出候选计算/持久提交/发布的边界；异步代码注明线程、所有权、epoch 和退出顺序；迁移代码解释旧字段到新字段的对应。注释优先说明约束与原因，避免逐句重复代码。
