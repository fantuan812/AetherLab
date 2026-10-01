# 启动排空等待与正式前端停留点联合契约

状态：接口草案。实现、原生地图资产与集成审阅完成前，不将后台半套或不存在的默认地图合入 main。

## 触发依据

- `UAetherNativePersistence::StopScene` 撤销输入后调用 `UAetherCommandRuntime::DrainBackend`；超过现有有限同步预算，旧 Runtime 仍持有 Store、已接受命令/事实并继续 Tick 排空
- 新地图 `AAetherFrontierMode::InitGame -> Prepare` 目前把 `HasBackend` 直接作为错误写到 InitGame Error，因此暂时排空变成启动失败
- 旧 Store 的关闭与已接受事务收尾仍由旧 Runtime 唯一拥有；新请求等待不取得它的写权限
- 现有 UI 在 `AAetherFrontierHUD::BeginPlay` 才创建；PC、HUD、GameState 产生前没有本项目加载页
- 当前 `GameDefaultMap` 是 `L_Frontier`。UE `ReturnToMainMenu/HandleDisconnect` 会回默认图，不能充当不会再次 Prepare 的安全前端

## 后台启动状态

1. 验证请求世界、保存前缀与配置。真实参数错误立即返回原错误，不吞掉
2. 旧 Runtime 若是 active 而非 draining，拒绝冲突，不为新请求停止正在进行的正常游戏
3. 旧 Runtime 明确 draining 时，Prepare 接受一次启动请求，绑定世界、唯一 attempt、前缀、开始时间与配置预算，进入 `WaitingForBackend`；此时不开 SQLite
4. Tick 非阻塞观察旧后端。只有 `HasBackend == false` 才打开请求自己的数据库一次，然后进入既有 Inspecting/Auditing/Prepared/Activate 链
5. 开库、审计、恢复失败进入 Failed，保留数据与服务器原始诊断，不自动重试或自动换存档
6. 配置预算到期进入明确 Failed（`BackendDrainTimedOut`）；这只结束新请求，不能 Close、替换或声称回滚旧 Store
7. 当前本地主机取消新准备时，校验世界与 attempt 后失效它并进入 Cancelled；旧 Runtime 继续排空。取消不是命令回滚证明
8. Failed/Cancelled 不自行重新准备。只有新的显式本地 Start/Retry 意图可建立新 attempt

`BackendDrainTimeoutSeconds` 由 `UAetherStartupSettings` 的 Game 配置读取，要求有限正数且计算出的截止时间有限；缺失/非法配置作为启动配置错误。时钟使用单调时间，不使用地图 WorldTime。

## 只读状态与 UI API

Gameplay 负责新增 `Startup/AetherStartupState.h`、`Startup/AetherStartupClient.h/.cpp`。

`EAetherStartupStage`：`Idle, Frontend, Connecting, WaitingForBackend, ReadingStorage, Auditing, Restoring, WorldReady, Ready, Failed, Cancelled`。

服务器 `FAetherStartupSnapshot` 仅含：

- `FGuid AttemptId`
- `uint32 Sequence`，同一 attempt 内只递增
- `EAetherStartupStage Stage`
- 有界 `EAetherStartupFailure FailureCode`

不发送数据库路径、保存前缀、原始错误文本、账号信息、目标 URL 或事务负载。UI 将失败码映射为中文提示；详细诊断只保留在服务器日志。

`UAetherStartupClient : UGameInstanceSubsystem` 提供：

- `const FAetherStartupView& GetView() const`
- `OnChanged` 通知
- `bool RequestStart(FGuid LocalAttemptToken)`
- `bool RequestCancel(FGuid LocalAttemptToken)`
- `bool RequestRetry(FGuid LocalAttemptToken)`

`FAetherStartupView` 为只读本地值，含 `LocalAttemptToken, ServerAttemptId, Stage, FailureCode, bCanStart, bCanCancel, bCanRetry`。按钮可用性属于当前本地路由能力，不接受服务器指定按钮行为。

Client 子系统唯一处理本 GI 的 PreLoad、网络失败、Travel 失败和当前 Controller 的可靠 owner RPC；UI 不另建 attempt 或加载订阅。RPC 入口 `AAetherPlayerController::ClientV10StartupStatus` 只更新当前本地 Controller/World 对应缓存；旧 Controller、旧 world、旧 attempt、乱序 sequence 和本地已取消 attempt 的消息均丢弃。

本地权威启动也通过同一状态缓存 API 投影，不让 UI 直接读取或调用 Persistence/Store。

`WorldReady` 只代表服务端世界恢复就绪。客户端仍显示“同步角色”，直到当前 CommandClient 的拥有者快照、通道和当前 Pawn 就绪，才能发布本地 `Ready` 并隐藏加载遮罩。

## GameState/PC 之前的可见性

- UI 用本 GI 的 StartupPresentationSubsystem 创建 CommonActivatableWidget，viewport 与 LocalPlayer 可用后挂载顶层；不等待 Pawn、HUD 或 GameState
- 前 PC 阶段只能显示本地已知的“正在连接/载入”，不得伪造服务端正在等待数据库的结论
- 首个 owner RPC 到达后才显示具体服务端阶段；已存在的 Controller 在状态变化时接收一次快照，晚加入 Controller 接收当前快照
- 不承诺引擎同步 LoadMap 阻塞阶段的动画或交互；viewport/LocalPlayer 尚未产生时没有可操作的本项目按钮
- CommonUI 保持唯一输入配置者。UI 不直接 SetInputMode、关闭编辑器窗口或全局取消其他连接

## 正式前端与路由权限

- 新增正式 `AAetherFrontendMode` 与作者化的 `L_Frontend`。该模式不 Prepare、不安装命令后端、不创建玩法 Pawn，也不伪装房间列表或平台大厅
- Game 配置明确指定前端地图和可玩地图；入口由本地用户操作发起，不从服务器错误消息解析地址
- RequestStart 只从已验证的正式前端进入配置的可玩目标
- RequestRetry 只使用本次本地引擎上下文中保存并验证的原目标；不能改存档前缀、身份、平台、任意 URL 或自动重试。仅数据明确授权的目标可用
- RequestCancel 校验本地 token 后取消本次连接/启动，并转到已验证的正式前端目标。不得调用 `CancelAllPending`；有 pending 连接时只处理本 GI/context
- 远端客户端没有取消全服务器 Prepare 的 RPC。其取消只离开自己的连接；本地主机离开新启动才经权威本地入口失效自己的准备 attempt
- 在本地主机回到前端后，旧 Runtime 仍可 Tick 排空，前端不能抢关它的 Store
- 前端目标未产生、未被验证或当前模式没有安全路由时，按钮不可用并说明原因。不得回退到 Frontier、关任意窗口或假报取消成功

本阶段不选择 EOS/Steam 等在线平台，不新增 DevProfile 快捷认证，也不声称具备真实会话发现/建房/Join。

## 独占文件边界

后端/路由实现线：

- `Source/AetherGameplay/Public/Persistence/AetherNativePersistence.h`
- `Source/AetherGameplay/Private/Persistence/AetherNativePersistence.cpp`
- `Source/AetherGameplay/Public/Startup/AetherStartupState.h`（新）
- `Source/AetherGameplay/Public/Startup/AetherStartupClient.h`（新）
- `Source/AetherGameplay/Private/Startup/AetherStartupClient.cpp`（新）
- `Source/AetherGameplay/Public/Startup/AetherStartupSettings.h`（新）
- `Source/AetherGameplay/Private/Startup/AetherStartupSettings.cpp`（新）
- `Source/AetherGameplay/Public/Framework/AetherFrontendMode.h`（新）
- `Source/AetherGameplay/Private/Framework/AetherFrontendMode.cpp`（新）
- `Source/AetherGameplay/Public/Framework/AetherPlayerController.h`
- `Source/AetherGameplay/Private/Framework/AetherPlayerControllerV10.cpp`
- `Source/AetherGameplay/Private/Framework/AetherNativeScene.cpp`
- `Source/AetherGameplay/Private/World/AetherFrontierWorld.cpp`（只启动/登录状态发送；不动 NPC/遭遇逻辑）
- `Source/AetherGameplay/Private/Tests/AetherStartupLifecycleTests.cpp`（新）
- `Config/DefaultGame.ini`（只 Startup 配置区）
- `Scripts/Authoring/PrepareFrontend.py`（新；沿现有作者化工具链）

UI 实现线，独占以下五个新文件：

- `Source/AetherUI/Public/Startup/AetherStartupPresentationSubsystem.h`
- `Source/AetherUI/Private/Startup/AetherStartupPresentationSubsystem.cpp`
- `Source/AetherUI/Public/Startup/AetherStartupOverlay.h`
- `Source/AetherUI/Private/Startup/AetherStartupOverlay.cpp`
- `Source/AetherUI/Private/Tests/AetherStartupPresentationTests.cpp`

集成线最后处理：`Content/AetherCore/Maps/L_Frontend.umap`、`Config/DefaultEngine.ini` 的默认图、`DefaultGame.ini` 的 Cook 地图条目，以及本契约的完成/缺口更新。未生成真实地图时，不把默认图指向不存在的包。

如进一步检查需要新共享文件，先更新边界并协调，不能各自改对方文件。

## 回归与合入门槛

需要补源码覆盖：旧后端在途任务未排完时不开新库；排空后只开一次；超时/取消只失效新 attempt；旧写最终确认；新请求读取确认后的版本；旧启动 RPC/旧按钮 token 不复活；无 PC/无 GameState 本地状态；UI 销毁撤订阅；Frontend 不 Prepare；失败/取消不回 Frontier 自动重开。

后端、UI 各自独立工作分支 checkpoint；完成后统一集成审阅，不能将任一半套合 main 后宣称完成。

按当前 AGENTS：未编译、未测试、未启动 UE、不进行 UE 验收。若当前执行环境不能生成真实 `.umap`，只交付源码/作者脚本草稿并明确原生资产缺口；不安装 UE、不以脚本存在替代资产完成。

## 引擎接口依据

- [UGameInstance::ReturnToMainMenu](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/UGameInstance/ReturnToMainMenu)：要求 World，返回默认地图
- [UEngine::HandleDisconnect](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/UEngine/HandleDisconnect)：限定 World/NetDriver 的断开入口
- [UGameViewportClient](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/UGameViewportClient)：每 GI 对应视口，不依赖玩法 HUD 的生成

具体取消/重试引擎调用仍须在实现阶段按准确 UE5.8 API 和本 GI/context 核对；不得以设计稿当作已经运行验证。
