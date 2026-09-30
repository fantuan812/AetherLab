# 当前任务快照与数据化导航

本批替换任务追踪/HUD/地图的旧 Profile 读取链，并把私人训练的任务/前置/火点身份归入同一个生成定义。其他交互选择器的旧 int32 版本、世界投影、历史库存/服务实现另行清理，不混入同一个提交。

## 实现

- SelectQuest 直接接收 FAetherProfileStateV10，使用 AetherQuestProgression 的可用性/完成性；不提供旧参数重载或中间 Profile DTO
- Resolve 显式接收调用方当前快照。HUD/Map/Journal 均来自同一个 LocalPlayer 的 UAetherCommandClient，监听服务器也不能替换为更早提交的服务端Profile
- 返回表现结果带实际 ProfileRevision；无快照时显示同步中、不显示完成、不保留旧地图marker；换Pawn时清掉原指引
- Guidance.json 只声明训练准备、私人目标选择、遭遇阶段与现有锚点/遭遇中心的关系及文案。没有世界坐标副本、任务ID的运行时特殊分支或新任务管理器
- 训练使用永久技能账本，已完成目标不要求重建；私人目标必须归属当前角色，私人人形目标必须存活
- 遭遇目标借用 ActivityRewards 的 Objective 关联，只给参与者显示活动阶段；Idle/Failed/Succeeded保留基础目标
- 仅全部主线已Claim且目标日常服务已解锁时显示完成；任务已Claim但PendingRewards未领取时继续导航，并提示可单独领取奖励
- Journal用同一规则查询；锁定任务不能追踪，回调固定原Quest ID并重新检查当前快照，而不借变化后的Selected执行旧按钮

## 数据与模块边界

当前Guidance schema 1。缺文件、未知字段/大小写、重复条目、无效Quest/Objective/Skill/锚点/阶段、歧义目标均拒绝，不回退旧字面值。纯定义位于AetherCore；Actor定位和拥有者查询仍在Gameplay；控件只接收当前只读快照。定义文件由现有Definitions NonUFS staging携带，并进入FAetherV10Definitions/ValidateV10Content的聚合校验。

OwnedService 的允许来源仅为 Rules.PersonalTraining.FireObjectiveId 对应的实际训练生成器，或 WorldObjects 已声明的 Service；普通 Objective 存在不构成服务能力。运行时仍要求目标真实存在且归属当前角色，配置通过不证明该目标已生成。PersonalTraining 的三个身份必须精确匹配当前任务图，前置必须属于该训练任务的直接前置，火点必须是该任务的私人、不可交互补记目标。当前和待退役旧训练入口共同使用该生成身份，灭火归属检查也使用同一分类；原生训练入口保留单生命去重、已完成不重建与原生事务行为。旧 AetherFrontierInteraction 入口本批仅同步生成身份，仍可能向已有火点注热且未按 Evidence 跳过；这段残留行为随旧入口退役另行处理。当前 Rules 与规则测试 fixture 一起更新；没有存档字段或版本变化。

## 保留并更新的验证覆盖

- 新Core定义回归：版本、字段、重复条目、未知引用、歧义目标、不允许复制坐标；普通任务目标不能冒充OwnedService、私人生成关系/身份大小写、缺失训练定义、修改实际生成身份后旧导航拒绝
- 新Gameplay查询回归：支线循环、无快照、未加载持久位置、私人归属、永久技能、已完成目标、待领奖励、参与者阶段、完成条件和只读revision
- AetherGuidanceCheck保留原支线循环/实际位置/私人目标/公共火委托板/真实灭火/救援推进断言，改为唯一隔离命名空间；测试前置经可信服务器事实事务合成，并逐步等待Client快照。没有直接改PS->Profile，没有Publish伪造视图，没有旧同步Interact替代原生命令
- 火点绕过检查实际走NativeInteraction Submit并等待服务器NotReady；救援实际走同一服务并等待持久成功与客户端快照屏障。探针记录发送前给出的真实持久 CommandId，只接收同通道同ID回执；Busy/StorageUnavailable 只消费一次，等待生产客户端对原字节的有界重试，收到重试耗尽的瞬时拒绝或阶段超时明确失败，不能借其他操作的回执通过
- NativeJourney增加每帧当前快照版本导航断言；MenuInteraction保留Slate页签输入，并增加实际Journal锁定按钮回调与Map marker的数据一致性检查

全部新增/更新的UE测试均未执行。未编译、未启动、未做多人/磁盘/Cook。短探针包含明确synthetic setup，不把前置合成称作完整主线游玩，也不把查询/回调断言称作已验证的全部渲染UI旅程。

## 仍待完成

日常委托明细的Patrol/DailyFire坐标列表尚未统一到当前地图导航；本批完成页只导航到已解锁公告板。完整world/profile旧类型依赖、最终当前格式边界，以及音频、叙事、房间生命周期等原需求仍在后续工作中。
