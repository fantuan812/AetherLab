# v9 架构收口与区域状态

基线：`906ff410b9d337b806a29da66f7822991d8bdfd7`。依据《AetherLab 可扩展性、流送与背包专项审查》的 V9-01～V9-07 实施。本文区分代码交付、轻量验证与仍需独立执行的正式验收。

## 实现范围

| 工作包 | 本轮实现 | 主要入口 |
|---|---|---|
| V9-01 | 稳定实例 ID、期望版本、命令 ID；指定数量拆分/指定目标部分合并；持久回执、原因码与最终版本；UI 稳定选择 | AetherInventoryCommand / InventoryService / FrontierPanel |
| V9-02 | 统一候选奖励与使用规则；定义驱动的消耗品、商品表、掉落表、任务/日常奖励；共享掉落仍原子认领 | AetherItems、Rules.json、ClaimLoot |
| V9-03 | 库存 DTO/算法与档案、PlayerState/GAS 分离；库存、队伍、世界服务提取；可替换存储适配器 | AetherInventoryData、AetherProfile、AetherPlayerState、IAetherSnapshotStore |
| V9-04 | 物件定义、布局、能力、支撑/水端口/铰链参数装配；通用受水能力接口；第三处 Field 场景 | WorldObjects.json、AetherWorldAssembly、AetherWorldCapability |
| V9-05 | Actor 之外的世界记录合并仓库、弱引用实体注册表；距离激活/真实销毁、局部恢复、跨区位置与身份 | AetherWorldState、AetherRegions、ReactiveWorldSubsystem |
| V9-06 | 基础内容异步预载、装备异步视觉资源、句柄取消/释放；出生/回城就绪门；区域/cells/求解/接触/内存日志 | AetherAssetPreload、AetherTravel、AetherEquipmentComponent |
| V9-07 | 纯规则、隔离存档故障/重启、双远端客户端竞争/重复/旧版本/重连回归；源代码指纹 | AetherV9Tests、ReactiveV9Tests、CheckV9.ps1、TestV807Closure.ps1 |

角色与场景继续使用 UE Manny、BasicShapes 与既有占位资源。第三场景使用提交的 JSON 在运行时装配，中心约为 (-14000,10000)，沿用原 World Partition 地图与烘焙外壳，没有新增待提交的二进制地图或外部美术。

## 协议和事务边界

库存命令携带 `CommandId / ExpectedInventoryRevision / Action / ItemInstanceId / Quantity / DestinationInstanceId / DefinitionId / ShopId`。服务端先确认既有回执，再检查授权、版本、容量及距离；候选档案和回执一起写入成功后才发布状态、更新装备或应用药品效果。相同 ID 的不同载荷拒绝。同 ID 的合法重复请求返回原结果。

当前期望库存版本使用保守的整个 `Profile.Revision`：其他档案事务也会使旧库存选择失效。最近 64 条成功回执持久保留；窗口外旧命令仍因旧版本被拒绝，但不承诺永久返回旧回执。UI 按 GUID 保留选中项，不再在数组压缩后自动操作另一个实例。部分合并通过回执返回实际转移数量，余额留在源堆。

金币及多物品奖励在同一个候选副本上计算。共享掉落把候选背包和 ClaimId 账本共同提交，成功后才移除世界实体；老掉落的 Definition+Count 仍作为 Items 为空时的兼容读取。账本维持 128 条上限，清理已领取记录；未领取满额时拒绝新增，不静默回收玩家可见掉落。

`FAetherInventoryData` 不包含角色/GAS 类型；`FAetherProfile` 保留任务与成长，并继承库存数据域。服务执行仍由权威 GameMode 协调同一份数据库，提取为各服务源文件，没有用彼此独立提交破坏跨域一致性。OwnerOnly 档案复制仍采用 SerializeBin；没有把 32 格灰盒无测量地替换为 Fast Array。

`IAetherSnapshotStore` 提供 Exists/Load/IsCommitted/Publish；默认本地适配保留双世代数据+校验标记。目标代先失效，再写数据、读回、发布校验标记。数据写后故障不会发布候选状态；仍可读前一已提交世代。此协议不是数据库级断电持久保证。

## 定义与兼容

`Rules.json` SchemaVersion=2：容量、Uses、Shops、LootTables、Dailies、InteractionRequirements 和 SpellUnlocks。现有使用策略为生命/法力/体力补充以及冷却、安全时间；新机制仍需要对应策略实现。日常模板、门槛、事实、消耗与奖励已数据化，刷新周期继续使用原 UTC 日。

`WorldObjects.json` SchemaVersion=1，含 136 个布局项。支持已定义的 Metal、Carry、Impacts、PowerSource、PowerReceiver、Reactive、Moving、Bridge、Buoyant、LiquidSource 能力；未知能力和失效引用拒绝。布局的 AllowAbsentFromOlderSave 显式控制新增对象兼容，不依赖 Field/Workshop 名称。服务标识选择已有交互动作，同机制第三场景无需修改 Make 或主交互分支。

现有档案属性名保留；世界记录新增 RegionId。未知物品或不兼容世界材料/身份按验证规则拒绝；世界恢复失败后禁止覆盖原快照，不悄悄把不兼容记录当新世界保存。定义扩展不等于任意旧存档自动迁移，改变稳定身份或材料签名需明确迁移设计。

## 区域与资源生命周期

区域定位格为 7000cm，激活按所有真人位置并集计算：未加载 8000cm、已加载保留至 9500cm，减少边缘反复切换。携带中、仍在运动的物件保活；支撑依赖共同加载/卸载。铰链布局不允许自身作为可流送对象。电/水连接在对象注销后断开，不对离线对象继续供能。

卸载前先保存所有已加载状态并合并进仓库；保存失败则保留 Actor。离线记录不会被一次不完整 Capture 清空，区域恢复只修改本次加载的稳定身份。跨区移动以当前/持久位置计算，出生点不会再生成第二份。局部恢复保留其他区域的输入去重与事件队列，存在待结算输入时拒绝恢复。

离线区冻结，不追算离线燃烧、传热或水流。地面与必要服务保留；启动阶段仍先装配定义对象再执行区域调度。本轮实现了运行时活跃集合，并非所有启动对象都已实现首次按需加载。

共享 Manny/基础装备 bundle 在世界生命周期内保持预载；装备独立请求在切换/结束时取消释放，世界退出释放共享句柄。资源可被其他 UObject 引用继续持有；没有宣称每个区域都有独立资源包或销毁 Actor 就立即释放全部显存。

出生、回城和复活传送先暂停移动/碰撞，再等待资源、World Partition、地面和有导航标记地图的导航就绪。超时退回原位置；等待期间禁止世界/库存交互。远距传送清除旧移动基座，并同步客户端定位，防止旧基座导致校正被忽略。

## 验证

实际结果、源码指纹和日志索引见 [Verification-v9.json](Verification-v9.json)。测试在最终提交前的同一工作树执行，随后仅删除两个新增文件的末尾空行；记录分别保存测试指纹与最终源码指纹。指纹按源码、测试脚本与定义的规范化内容计算；不把基线 SHA 误写为本轮已测提交。提交检出后可用 `Scripts/GetV9SourceFingerprint.ps1` 比对。

可复现入口（PowerShell 7）：

```powershell
.\Scripts\Build.ps1
.\Scripts\CheckV9.ps1
.\Scripts\TestV807Closure.ps1 -Port 7798
```

单元过滤器为 `Aether.V9.+Aether.V802.+Aether.V5.+Reactive.Frontier.+Reactive.V9.`。定向网络运行使用独立编辑器 dedicated 进程和两个远端客户端；为固定物件断言显式保持区域加载。区域并集检查使用两个位置输入，不冒充两名真人分散流送的验证。

背包截图已人工查看：稳定选择、数量输入、合并目标、商品选择和操作按钮正常显示。首次区域测试把物件放在格边界，受到物理小位移影响；改为格内部坐标。联机回归发现出生就绪前操作和远距传送旧基座两处时序问题，修复后重新执行。晚加入燃烧夹具在测试标志下明确配置 1kg 燃料，覆盖编辑器冷启动时间；正式场景默认燃料不变，仍检查真实燃烧与复制，而非跳过火焰断言。

最终短流送样本记录 cells 0～8、反应体 10～18、求解约 0.054～0.099ms、接触约 0.057～0.173ms、进程内存约 1650～1662MB。数字受机器、后台编辑器和采样时间影响，只证明日志来自实际加载/卸载过程，不是内存长期有界或正式性能验收。

## 独立验收与条件扩展

本轮未执行四人/Listen、网络丢包延迟注入、全图连续流送与 HLOD、长期内存/复制/导航性能、全量 Cook 或发布包。旧存档跨版本完整资产矩阵、两个真人分散激活集合和快速往返长期稳定性仍需独立验收，不能由本轮短样本推定通过。

耐久/绑定 StackKey、有状态物品丢弃再拾取、多容器拖放、个人/队伍拾取策略、任意新装备行为与 Fast Array 是审计中条件性扩展，不作为本轮已经交付的功能。现有共享先到先得策略继续由权威事务保证。纯规则类型已去掉 Frontier/Combat include，但本轮测试仍由 Editor target 执行，没有声称新增了独立无引擎构建目标。
