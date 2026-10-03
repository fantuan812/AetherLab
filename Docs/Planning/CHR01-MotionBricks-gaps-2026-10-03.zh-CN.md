# CHR01 动作适配核查 · 2026-10-03

基于 main `3d23dda597c8c9e0833137f8bd04bfb9bf17ccbb`。只读源码/目录与上游文档核查，未编译、未测试、未运行UE/推理/Cook；没有安装或下载模型。本记录不授权取消此前UE验收边界。

## 已有接口，不能再造兼容层

- `Build/ThirdParty/MotionBricks.lock.json` 固定 LocalAI `motion-bricks.cpp` `ee0cf5d9035f639ed0787f390fb1ce05d6a4c463`、GGML `8c63e70982c95ceb862e3a1073a2c1beef75d60a`、C ABI 1及 G1 GGUF模型版本。它不是直接加载NVIDIA Python checkpoint
- `Plugins/AetherMotion/Source/AetherMotionRuntime/Private/MotionBricksApi.{h,cpp}` 和 `MotionBricksRequired.inl` 已有动态库验证、模型/风格/agent API及无状态边界推理API。`MotionBricksScheduler.cpp` 已有串行工作线程与有界请求队列。本次不添加第二层适配器
- `Content/AetherCore/Definitions/MotionBindings.json` 仅Manny、Quinn为configured；旧Quaternius65仍draft。CHR01银发V2虽同为65骨，也不能当作旧身体完整资产身份

## 真实缺口

1. CHR01实际UE SkeletalMesh/Skeleton完整身份和导入后轴映射未知；旧draft的target_mesh与source_heading_degrees为null。Blender米制、Z-up/-Y-forward不是UE已导入证明。当前仓库树无CHR01/Quaternius原生uasset
2. 对应65骨IKRig、双向G1 Retargeter、MotionProfile、动画类、传统/受控动作绑定未生成；draft的actions/locomotion/jump/fall/land/heavy/light为空。已有G1/Manny/Quinn资源不能冒名替代
3. 输入必须是兼容G1Skeleton34的30FPS canonical Y-up米制姿态。项目已选择C ABI路线；银发65骨/24FPS握持片段需要真实骨链/参考姿态重定向及时间采样，不能把65骨数组或Blender原坐标直接塞入34骨API。此处是缺输入资产与验证，不是缺一个猜测转换函数
4. 当前 `Build/ThirdParty/MotionBricks.bundle.json` 记录模型/风格hash与字节数；`MotionBricksApi.cpp::VerifyStage` 要求平台stage.json、动态库/GGML依赖及5个g1-f32文件。Git树不含部署制品，本轮没有检查用户机器部署目录，不能声称用户机器一定缺模型，也不能凭bundle清单声称已部署
5. `Scripts/Build/StageMotionRuntime.ps1` 当前作者入口写Win64 stage；runtime同时包含Linux加载分支。因此Linux加载代码存在不代表已有对应Linux制品/打包流程已验收
6. PR13仍draft，原生装备目录迁移、目标mesh/import轴未完成。PR37的Blender七武器合同不会自动补齐。弓右手draw是动态合同，不能误用fixed-support矩阵

## 上游核查来源

- [NVIDIA官方项目页](https://nvlabs.github.io/motionbricks/)：公开初始release描述为G1交互demo及训练流程；页面中的UE5演示不是可直接替代本项目原生资源的65骨插件交付
- [NVIDIA官方源码目录](https://github.com/NVlabs/GR00T-WholeBodyControl/tree/main/motionbricks)：README列Python/CUDA与G1 checkpoint流程；不要把其依赖表当作项目所选LocalAI CPU/Vulkan C ABI部署说明
- [所锁定LocalAI API文档](https://github.com/localai-org/motion-bricks.cpp/blob/ee0cf5d9035f639ed0787f390fb1ce05d6a4c463/docs/API-INFERENCE.md)：边界为两组各4帧、每帧34骨local XYZW及root；30FPS、canonical Y-up；推理接口本身不做风格选择、世界放置、接缝混合或物理可行性保证
- [锁定C头文件](https://github.com/localai-org/motion-bricks.cpp/blob/ee0cf5d9035f639ed0787f390fb1ce05d6a4c463/include/motionbricks/inference.h)：与项目所需无状态边界API对应

结论：现有接口和锁定依赖路线已存在；目前不能把CHR01标为运行接通。缺口是目标原生资产/轴与动作绑定、已验证部署制品及尚未授权执行的运行验证。没有新建兼容层，也没有将模型输出授予GAS命中、伤害或物品提交权限。
