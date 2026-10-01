# 背包拖拽按下身份

## 源码确认的问题

`UAetherInventoryPage::Refresh` 在背包容量未变时复用格子，`UAetherInventoryCell::Present` 更新格子的 `Request`。旧实现左键按下只申请 `DetectDragIfPressed`，到 `NativeOnDragDetected` 才读取这个可变的 `Request`。

因此，按住物品 A 后、越过拖动阈值前，如果同步把同格换成物品 B，实际发起的拖拽会捕获 B。落点的既有 `RevalidateIntent` 看到的是合法的 B，无法还原玩家最初按住 A 的意图。这是未发送 UI 意图换目标，不是服务器事务越权。

Epic 的 [DetectDrag 文档](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/UMG/UWidgetBlueprintLibrary/DetectDrag) 明确检测请求与后续越过阈值事件分离。

## 改动

- 左键按下时，在焦点与选择回调之前保存 inspection 请求的值身份；同步回调不能把它换成后来格子中的对象。
- 精确校验 Owner、Session、目标种类、实例、逻辑槽、容器、定义、物理槽和既有依赖键。无关的展示版本变化保留同一按住意图。
- 同格换物品、相关依赖变化、筛选隐藏会立即撤销；随后恢复原值不会复活旧按压。
- 拖动检测一次消费原值，并检查原 user/pointer/touch 类型与仍按下的鼠标键。鼠标释放、捕获丢失、焦点路径移除、拖拽取消、NativeDestruct 和 Slate 资源释放撤销尚未消费的源。DetectDrag 并不持有鼠标捕获，故真实失焦有独立撤销入口。
- 单纯 MouseLeave 保持原有行为，避免假定引擎事件顺序而误取消跨格拖拽。键盘/手柄仍使用既有 PickUp/Place 路径；没有新增触屏路由。
- 落点和提交继续走已有 inspection 复验与权威命令服务。不修改 CommandClient，不生成、改写、清理或重发已提交命令字节及 CommandId。

## 已核查的现有覆盖

背包关闭/重开已有 `ResetPresentation`；确认前已有 `Refresh` 和 `InspectionSession::Confirm`；Client 持有独立 pending 队列，关闭页面不删除其原命令，重试仍复用原字节。此次不重复改这些路径。

## 回归源码与限制

新增 `Aether.V10.UI.InventoryDragPressIdentity`，直接调用生产捕获/消费函数、`Present` 及取消/销毁回调，并通过真实 `NativeOnDragDetected` 检查替代物品拒绝。覆盖同身份版本刷新、换物品、数量依赖、Owner 大小写、Session、槽/容器/种类/定义、筛选恢复、一次消费、MouseUp、capture lost、焦点离开并恢复、离格、cancel、关闭/Slate 重建、跨 pointer/user/touch、手柄原路径和重新按下。

按 AGENTS 实现阶段约定：**未编译、未测试**。没有运行真实 viewport 的鼠标阈值/焦点路由、触屏设备、RPC、Travel 或 UE 验收。测试源码的函数级事件覆盖不能替代这些运行验证。
