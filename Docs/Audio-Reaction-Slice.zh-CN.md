# 反应音频首个纵向切片

## 实现状态

代码与六个真实来源 WAV 已准备；**未编译、未测试、未导入 UE、未听审、未打包**。
没有 UE 5.8 执行器，本批无法生成 `.uasset`；在完成导入之前，实际游戏返回 `Unavailable` 并保持静音。
源码与 WAV 不是“可玩音频已交付”的证据。没有正弦提示、空引用假成功或自造 PCM 运行时。

## 边界

- `AetherAudio` 仅依赖 Core/CoreUObject/Engine/DeveloperSettings，完全不依赖 Gameplay、ReactiveWorld 或 UI
- Gameplay 只消费稳定事件 ID 和位置；原有 `LastFeedback` 转换去重仍负责同一状态不重复请求，首次快照不播放
- Break：进入破坏反馈；Freeze：进入承重冰面反馈；Extinguish：进入已熄灭反馈
- 保留现有反馈优先级；并不将所有物理状态变化当成独立网络音频事件。本批不添加伤害、脚步、音乐或新 RPC
- 专服在 subsystem 创建、初始化、播放三个入口均拒绝；不会构造音频组件

## 单一数据来源

`ContentSource/Audio/catalog.json` 是事件映射、变体、gain、冷却、并发预算与衰减距离的唯一可编辑权威。
对应 WAV 位于 `Waves/`；选中原始文件位于 `Originals/`，manifest 保存各层 SHA-256 和 CC0 来源。
`Content/AetherAudio/Generated/g_<源摘要前缀>_<独立运行ID>/` 中的 SoundWave / 目录全部由导入器派生，禁止另行编辑生成资产混音值。
每次构建使用全新路径，即使前次中断也不覆盖用户旧资产；旧目录和部分生成目录保留，不自动删除。
资产 source ID 必须为小写；事件 ID 与变体/路径碰撞按 UE 不区分大小写身份校验。
C++ 只定义稳定契约与有效性约束，没有资源路径或某事件的专用混音参数。

配置入口为 `UAetherAudioSettings.CatalogId`；Asset Manager 扫描 `AetherAudioCatalog` 主资产类型，默认 NeverCook；发布时只为选中的主资产写 AlwaysCook 规则，
其 `Audio` bundle 保存软声音引用。未选中旧世代不主动加入 Cook；实际依赖收集仍须 Cook 实测。运行时只在世界开始时解析目录并同步预载这个小型六变体集合；反应热路径不读盘。
后续若目录增长，应另立预载生命周期改造任务，不在此切片建立自定义媒体框架。

## 行为

- 变体按目录顺序轮转；成功启动才推进轮转与冷却
- 冷却和最大并发为同世界、同事件共享；不是每 actor 一个预算
- native SoundConcurrency 使用 PreventNew，已有声音不会被新请求强制抢占
- 组件弱引用预算配合 native concurrency；结束组件被移除，切世界时停止并释放
- 数据不合法、重复事件 ID、任一变体缺失/加载失败、循环声音、无音频设备产生的组件失败：Unavailable
- 冷却/预算拒绝：Suppressed；接受组件：Started。Started 不承诺声卡输出、距离可闻或主观音质
- 某次转换 Unavailable 后不会每帧重试；需新的状态转换或重新进入世界。不会播放旧 beep 替代

## 来源与语义

详见 `ContentSource/Audio/CREDITS.md` 与 manifest。
木裂原素材用于当前原型破坏提示；冰冻使用作者设计的冰魔法；熄灭使用蒸汽嘶声作声效设计。
后两者不冒充物理实录。木裂对非木质材料的拟真适配未验收；未来若扩充材质，应以数据化事件选择扩展。
FFmpeg 解码/重采样、峰值归一化属于资源制作，不是游戏功能测试。所有六音保留完整时长，尚无听审。

## 统一验证阶段（全部实现完成后执行）

1. 离线：`python Scripts/Tests/TestAudioCatalog.py`，覆盖完整目录、重复 ID、空/坏引用、非有限 gain、预算、路径越界和原/处理源哈希
2. UE 5.8 增量编译 Editor、Game、Server；本批不会宣称没有运行的 UHT/编译通过
3. 启用编辑器 Python 插件后，用首个独立进程运行 `UnrealEditor-Cmd AetherLab.uproject -run=pythonscript -script="Scripts/Authoring/PrepareAudioCatalog.py --mode build --receipt <绝对路径>/Saved/AudioAuthoring/receipt.json"`（具体命令行引号按平台调整）
4. build 校验来源，将六个 SoundWave 与新目录保存到全新不可变路径；通过 AetherEditor 中的原生 JSON 作者桥写目录，避免依赖 Python 强写 VisibleAnywhere。此时不改变活动目录，也不宣称持久化已验证
5. 退出 build 进程，启动第二个独立 UE 进程，以同一脚本运行 `--mode verify-publish --receipt <同一绝对路径>`；不同 PID 检查仅拒绝同进程冒充冷读，不能证明 build 进程已退出；操作者必须确认 build 进程完全退出后再执行 verify-publish。逐项核对保存目录的源摘要、事件顺序、ID、全部混音参数、禁用原因、软引用顺序，及 SoundWave 属性/时长；检查配置的 AssetManager 能解析此主资产
6. 仅在上述读取符合时，保留旧 INI 备份，用 `os.replace` 单次替换 DefaultGame.ini，同时改变 CatalogId 和活动目录 Cook rule。发布锁仅协调本脚本，不能约束外部 UE 编辑器或人工同时写入 INI；操作期间必须避免其他进程或人工并改此文件。此原子边界仅限同一文件替换，不承诺正在运行的 UE、AssetRegistry、Cook 或磁盘掉电事务的一致性；重新启动 UE 后才激活。任何失败抛错保留上次发布选择，新源未生效；新建部分文件留下待人工审查，不删除旧文件。显式禁用事件使用 `unavailable_reason` + 空 variants 发布，新活动目录返回 Unavailable
6a. UE 自动化 `Aether.Audio.DefinitionContract`：无来源、显式禁止、空软引用、无效冷却/预算、重复事件 ID；增量编译也需覆盖新增 AetherEditor 作者桥
7. 小型 PIE：三个事件首次快照静音、每次转换只请求一次、重复同状态不重放；间隔内多对象受共享冷却限制；同时触发超过预算返回 Suppressed
8. 人工耳机试听六个变体与空间衰减，确认并发响度、freeze 语义、蒸汽尾音、近远距离混音；据听审改 JSON 后再导入
9. 故障注入：缺目录、缺一个 SoundWave、未加载/错误路径、重复 ID、无声卡；均不得返回 Started 或产生 beep
10. 专服短启动：确认无 AetherAudio subsystem / UAudioComponent；客户端单独短启动检查 local presentation，避免服务器重复发声
11. 对项目既定小型目标进行 cook/打包引用检查（不新增全量 Cook 或压力测试），验证主目录及六个 bundle 依赖被打包；无此证据不能称打包可用

### 已核对的官方 API 文档（仅接口依据）

- https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/DataAssetFactory
- https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/SoundWave
- https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/UGameplayStatics
- https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/USoundConcurrency
- https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/UWorld

文档核对不能替代 UE 5.8 编译、Python soft-reference 赋值实测或 packaged-runtime 验证。

## 第二轮源审修复状态

不可变生成、两进程比对、配置文件级发布、大小写碰撞及故障注入测试代码已编写，均未执行。
旧文档中“单次脚本保存后即读回验证”不构成持久化证据，已移除该流程。
新的机制同样只有真实执行成功后才能称为导入/冷读通过；目前仍无任何 `.uasset` 或 UE 执行结果。
原/处理声音哈希及原发布页 CC0 来源经过独立静态审查；这不代替音频听审。
