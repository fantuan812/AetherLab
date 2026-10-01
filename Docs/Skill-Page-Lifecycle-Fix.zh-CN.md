# 技能页快照生命周期修复

## 源码确认的问题

- `HandleMenu` 在 Pawn 更换时清空快照，却保留 `NativeSnapshotKey`。相同档案、授予、冷却版本重新出现时，`HandleNativeProfile` 按旧键提前返回，页面可能一直空白
- 缺快照与 Widget 销毁路径没有同步失效全部原生缓存
- 原缓存键没有覆盖 `bCanAct`。该值由 `PresentationReady && Ready && !HasPending` 生成；只改变动作门禁而不改变档案版本时，页面可能继续显示旧可用性

## 实现边界

- 使用现有 LocalPlayer CommandClient、菜单绑定 Pawn 与 GAS 展示快照，未新增游戏状态或事务写者
- 发布时单独比较 Pawn、精确 OwnerIdentity、SessionId；身份变化不能复用同版本缓存
- 保留现有真实冷却、施法、战斗、导师距离、技能授予更新链，同时比较最终 `bCanAct` 与 `bPresentationReady`
- 身份失效、缺快照、销毁时统一清理展示、选中项、未发送意图、拖动和缓存；已经提交的请求仍保留在 CommandClient，页面不重建请求 ID、不重发意图
- 普通关页保留同一有效身份的快照，重新打开不会无条件清缓存或重复刷新

## 回归源码与验证状态

新增 `Aether.V10.UI.SkillPageSnapshotLifecycle`，直接调用生产发布门和菜单换 Pawn 回调，覆盖：

1. 有效同版本去重；动作门禁关闭/重开；同步就绪状态变化
2. 普通关页重开；同版本换 Pawn；旧选中和 pending 展示失效
3. 大小写不同的拥有者与新通道隔离
4. 缺快照后同版本恢复；同一 Widget UObject 销毁后重新发布；菜单订阅撤销

按 AGENTS 当前执行顺序，只编写回归源码并审阅差异。未编译、未测试、未启动 UE，也未做 UE 视觉验收；以上不代表运行通过。
