# 当前系统改动的统一执行与验收清单

状态：**全部未执行**。这是后续 UE 环境中的有序清单，不是运行结果。按 AGENTS，代码和必要资产实施完毕后再进入统一验证；历史通过记录、静态复审、WAV解码和脚本存在都不能替代本候选的证据。使用同一明确候选，不启动全量压力或大规模Cook。

## 0. 固定环境和候选

- 记录提交、干净工作树、UE 5.8具体Build、插件版本、客户端/服务器目标；目录和源素材完整。MotionBricks按既有锁版本/哈希检查，不因云端缺模型推断用户机器缺失，也不自动下载大型引擎/模型
- 每个探针只用新的隔离 AetherSavePrefix。磁盘故障用例用复制的测试原件并保存前后哈希，不操作真实用户进度
- 不自动移动/删除旧存档让启动“通过”。未知格式/版本的正确结果是清晰失败并保留原件

## 1. 首次代码编译

1. 执行 `Scripts/Build.ps1 -EngineRoot <UE目录>` 的增量 Editor/UHT 编译，先修所有编译错误。此时不记录最终候选通过，因为后面作者步骤还会生成资产/修改配置
2. 核查 Game/Server目标的新增模块依赖（尤其 AetherAudio 无Editor依赖，AetherEditor作者桥只在Editor）；用现有目标增量构建，不把Editor编译等同客户端/专服已通过
3. 编译成功只记代码编译结果，不记功能/资源验收

## 2. 必要作者资源生成（必须先编译作者桥）

使用独立编辑器作者进程：`UnrealEditor-Cmd.exe <项目> -run=pythonscript -script=<脚本及参数> -EnablePlugins=PythonScriptPlugin -unattended -nop4`。具体路径/引号按执行平台调整。检查退出状态与作者输出，不把文件存在视作成功。

### 动作

- 运行 `Scripts/Authoring/PrepareControlledAnimations.py`，使用当前 Actions.json 与既有官方骨架/源动画
- 必須更新 `Content/Animation/Controlled/A_Vault.uasset` 的阶段姿态及 `DA_Actions` 等脚本实际受影响产物；审查差异并保留 `Saved/Authoring/Controlled/manifest.json` 的配方摘要
- 当前旧25段时长元数据与新值相容，不证明新Vault接触关键帧已经生成。必须在此步补交二进制

### 对话

- 运行 `Scripts/Authoring/PrepareV10UI.py` 按当前 WidgetLayouts 重生成WBP；脚本会涉及既有UI集合，审查所有实际差异，不只看输出目录
- 必须提交/检查 `Content/UI/Widgets/WBP_Dialogue.uasset`：顶部镜头空间、底部字幕/服务区，Speaker/Speech/Choices/Feedback绑定完好
- 现有二进制是近乎不透明整页布局，会遮挡镜头；源码中相机运行不代表新构图已经可见。禁止用运行时改造旧树代替这一步

### 反应音频（音频批合入后）

- 来源权威 `ContentSource/Audio/catalog.json` 和六个实际WAV；先执行 `python Scripts/Tests/TestAudioCatalog.py` 并检查完整结果
- 首个独立UE进程：`Scripts/Authoring/PrepareAudioCatalog.py --mode build --receipt <绝对路径>/Saved/AudioAuthoring/receipt.json`
- 确认build进程完全退出，再在第二个独立UE进程：同脚本 `--mode verify-publish --receipt <同一路径>`。不得在同一Editor缓存中代替冷读取
- 必须生成并冷读核对六个SoundWave、AetherAudioCatalog及其来源摘要/软引用/混音字段。成功后才按脚本文件级发布 CatalogId + Cook rule，重启UE才激活
- 保存旧INI备份；执行期间不得另有进程/人工写 DefaultGame.ini。未选中世代/部分产物不自动删除。此流程不是掉电事务保证
- 当前没有导入后的uasset，运行时Unavailable/静音是准确状态。详见音频批 `Docs/Audio-Reaction-Slice.zh-CN.md`

## 3. 固定最终源码+资产候选，再验证

1. 审查作者资产与配置差异，提交后保持工作树干净
2. `Scripts/Build.ps1 -RecordCandidate -EngineRoot <UE目录>` 记录最终相同候选的编译身份
3. `Scripts/Validate/ValidateContent.ps1`：检查当前Definitions、动画/骨架/时长、WBP绑定和资源可达性；不得跳过坏资产使报告变绿
4. `Scripts/Validate/TestRules.ps1 -Filter 'Aether.Systems.Actions.+Aether.Systems.Persistence.+Aether.Systems.Skills.+Aether.Systems.Guidance.+Aether.Systems.Dialogue.+Aether.V10.Interaction.+Aether.V10.Store.+Aether.Audio.'`。核对每个预期用例出现在新报告且真正完成，不只接受总计>0
5. 任何修复改变候选或资源，都重新生成匹配编译身份，不能将旧报告附给新树

## 4. 按依赖执行小型行为场景

### A. 存档与原子性

- CurrentSchemaStartup/UnsupportedAggregateSchema覆盖只是策略与内存SQLite用例；另外检查实际磁盘：仅.sav、仅.crc、空DB+旧档均明确失败，前后原件哈希相同
- 合法当前世界+旧原件旁置仍读当前世界；孤立聚合但无世界行不清零；未知32/64位版本均明确UnsupportedSchema
- 显式非法/空/超长SavePrefix不得写默认命名空间；合法新命名空间可新建
- 当前原生库重启/重连/checkpoint/奖励提交恢复，用既有 `Scripts/Validate/TestStoreCrash.ps1` 的当前流程。不要运行已退役Import/V4Smoke

### B. 技能与遭遇

- Profile玩家未就绪不能借NPC技能；玩家有PlayerState时不能选Definition授权
- 真实SpawnFighter/BeginPlay/GAS授予无重复；招募治疗同行者实际使用Water.Draw，服务器调整过rank不被重授重置
- 缺NPC定义或批量第二角色失败：清理部分生成，遭遇Failed/营地not-ready，不清场、不发奖励、不记任务信用；重试恢复有效数据后可正常生成
- Adventure装配失败时Interact/Save/Load都拒绝，隔离旧探针存档哈希不变

### C. 动作与导航

- Actions新值→作者重烘焙→GAS闪避/翻越时序与展示一致；取消/重入/方向stance拒绝不产生额外提交；GetUp与落地确实读目录
- `Scripts/CheckGuidance.ps1`：真实火点拒绝/灭火/救援命令与回执身份，合成前置必须保持报告中synthetic标识
- `Scripts/Validate/TestNativeJourney.ps1`：使用最终候选证据；任务恢复、待领奖励和每帧导航revision匹配当前Client快照
- `Scripts/Validate/TestMenuInteraction.ps1`：保留真实Slate输入；日志锁定按钮/支线切换和地图marker与同一快照一致，无快照不保留旧目标
- 故障：删改测试副本的Actions/Guidance/NpcSkills数据，应显示明确不可用而非退回编译字面值

### D. 对话

- `Scripts/CheckDialogue.ps1`覆盖实际Session调用及Register提交后关闭仍完成。伤害序号和前置是受控fixture；不能称实际网络伤害/渲染已验收
- **必须有渲染的人工场景**：新WBP不遮住镜头，分句可读，键鼠/手柄可立即Advance/Skip到服务，重复购物不被强制等待
- 快照变化只刷新选项不重播字幕；旧按钮失效；字幕完成/跳过本身无奖励或请求
- 实际受击、菜单离开、目标卸载/Pawn更换、断线、外部镜头接管、连续开关均恢复正确视角和输入，无残留本地相机
- 两个独立客户端的拥有者隔离，远端和专服没有本地对话相机；分屏不属于既定联机承诺，当前LocalPlayer持久身份不为测试改写；遮挡与小空间安全，不穿墙；进入/退出混合与移动恢复检查
- 关闭后已提交操作仍由事务服务完成，重新打开只显示当前快照结果

### E. 音频

- 有声卡、小型PIE/客户端真实试听：Break/Freeze/Extinguish首次快照静音，状态转换只请求一次；共享冷却/并发拒绝返回Suppressed
- 缺目录/缺变体/非法配置/无音频设备返回Unavailable，不返回Started或播放beep；Started仅表示组件已接受，不保证可闻/音质
- 试听全部六变体、空间衰减/并发响度/语义；如改源目录参数，重走构建+独立冷读发布
- 专服没有该音频Subsystem/组件；切世界释放组件和预算；旧世代目录不主动进入Cook

## 5. 小范围打包与证据汇总

- 按项目既定小型客户端/服务器目标检查 Definitions NonUFS staging、当前主资产Cook规则和软引用bundle收集；无打包运行证据不能称打包可用。不要因本清单扩大为全量Cook/压力测试
- 所有产物/报告绑定相同commit、源码树、二进制身份与引擎版本；保留关键日志、错误码、存档副本前后哈希和必要截图/听审记录
- 作者成功、编译成功、规则成功、行为成功、视觉/听感成功、打包成功分别记录。任一步blocked/not_run保持原样，不把源码合并当作完整游戏验收
- 全量原需求的房间生命周期、成长经济、剩余世界兼容拆除、表情/配音与后续美术内容不因本清单而自动完成；继续按独立工作单元交付
