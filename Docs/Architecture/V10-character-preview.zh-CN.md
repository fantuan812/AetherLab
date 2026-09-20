# v10 独立角色预览（代码实现，未编译、未测试）

本单元补充 V10-08。按用户最新顺序，所有剩余实现完成后再统一编译和测试。本单元只进行源代码人工审阅，没有运行编译、规则测试、启动检查或资产制作脚本；历史菜单验收不覆盖新增预览。

## 已写入的代码

- LocalPlayer 预览子系统拥有一个独立 FPreviewScene、一个非复制 Actor 和一个 768×1024 RGBA16f 渲染目标。预览不生成真实 Pawn，不接入 ASC、库存、存档、区域流送或真实地图。
- 只在背包页面打开时捕获，最高 30 帧/秒；白名单只包含预览身体与装备。关闭时取消异步请求、解绑事件、移除模型与材质引用并释放 RT。保留单个空场景到 LocalPlayer 退出，避免 FPreviewScene 析构触发反复全量 GC。
- 异步请求携带展示版本；回调复验版本、当前 Pawn 和活动状态。页面所有者令牌防止旧 Widget 拆除时关闭新页面的预览。加载期间仍清理失效上下文。
- 真实装备与预览共用 AetherEquipmentVisuals 的网格、Socket、GripTransform 和绝对缩放逻辑。预览仅替换发生变化的挂载组件。
- 身体外观来自 CharacterDefinition，独立待机只播放兼容骨架；缺失模型使用基础几何体，缺失动画使用参考姿态，缺失挂点明确提示。
- 包围范围包括身体和装备末端，提供鼠标拖动、滚轮、按钮、手柄右摇杆和扳机的旋转/缩放/重置。旋转不调用真实角色动作。
- 当前入口接到兼容背包页面；小型独立 UUserWidget 可作为 WBP_CharacterPreview 父类。完整 CommonUI、图形背包及正式布局仍属于后续工作。

## 资产制作代码与待交付项

Scripts/Authoring/PrepareCharacterPreview.py 编写了 UI 透明材质、RenderTarget 模板、Manny/Quinn 外观、预览 Widget Blueprint 和显式 Cook 标签的制作流程。脚本尚未执行，尚未生成或提交这些新 uasset。Scripts/PrepareBasicAssets.py 也补充独立待机引用，但没有执行。

透明路径使用 SceneColor HDR 的反向 alpha，由 UI 材质执行 1-alpha；材质缺失时明确提示并显示不透明背景。不能据此声称发布包透明度正确。

## 统一验证阶段的待办

编译与反射、脚本 API、正式资产生成、Manny/Quinn 挂点及站位、长武器完整入镜、透明度/抗锯齿、异步切页/换 Pawn、鼠标捕获释放、手柄、各分辨率、关闭后的 GPU/CPU 资源释放、Dedicated Server 排除 UI、Shipping Cook 均未验证。V10-08 保持 in_progress。
