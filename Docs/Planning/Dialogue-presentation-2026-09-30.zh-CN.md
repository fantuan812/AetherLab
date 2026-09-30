# 本地对话表现实现契约

范围：完善现有十二个服务/公告节点的分句字幕、快速前进/跳过、拥有者镜头和中断恢复。服务资格、提交和奖励仍由现有交互提供者与事务服务负责，不添加剧情任务管理器。

## 单一数据权威

Interactions.json 当前 schema 2；所有活跃消费者同步切换，不保留 schema 1/Text 转换。节点明确声明 Lines（Text、DurationSeconds）与 PresentationId，原 Speaker/Options 继续使用。表现目录集中按钮文案、是否允许前进/跳过及可选镜头构图、FOV、混合时间、探测半径；公告板明确 Camera=null，不创建相机。字幕时长/构图不得落回编译期默认值。缺失/未知字段、坏范围、缺引用拒绝。

## 执行边界与验收场景

1. 显示首句，按数据时长推进；超长一帧也有界处理。Advance 只推进一句，Skip 直接显示已有服务选项。二者不产生任务事实或命令。当前所有节点均可快速跳过，不强迫重复购物等待。
2. 同节点快照更新只刷新选项和版本，不重放首句。旧按钮不能升级成新版本意图；字幕播完后才显示/接受服务选择。
3. 镜头仅由所属 LocalPlayer 的当前控制器使用；保存进入前 ViewTarget。另一 LocalPlayer、服务器和其他玩家不接收相机变更。相机不复制，不写入持久世界。
4. 受伤序号变化、菜单离开、Pawn/目标失效、所属控制器/通道改变时关闭展示并归还视角。只在视角仍属于本会话时恢复，不能覆盖另一个系统后来接管的镜头。关闭和重复关闭无残留。
5. 无镜头配置不创建相机；配置镜头无法安全放置时明确拒绝该表现启动，不伪称镜头已播放。实际空间/遮挡观感需UE实测。
6. 已提交的持久命令由 CommandClient/服务器继续完成；关闭对话仅清理本地展示，不取消或回滚事务。重新开对话重新查询当前快照。

## 验证与未交付边界

实现期按 AGENTS 不编译/运行测试。需要补齐解析拒绝、确定性字幕时序、生产会话的中断/恢复/旧回调及服务提交后关闭回归；所有用例写完后仍须标未运行。实际分屏、相机混合/碰撞、网络受伤时延、UI焦点与资源Cook待统一验证。

本批不包含面部表情、配音、长剧情演出或镜头资产美术验收；不能据此称整套叙事已完成。使用现有WBP_Dialogue的Speaker/Speech/Choices容器，播放控制在既有动态选项区生成，无需新增绑定槽。WidgetLayouts.json 同步声明顶部透明镜头区域和底部对话面板；**现有 WBP_Dialogue.uasset 尚未重生成，仍为近乎不透明的整页面板，当前二进制会遮挡镜头，不能称新镜头版式已交付**。不在运行时重写旧WidgetTree。

相机由本地瞬时Actor持有，进入/退出均按数据自行插值，并始终使用明确的ViewTarget所有权；退出插值完成再销毁，任何引擎PendingViewTarget均视作外部接管，会话停止驱动且不恢复旧视角；临时Actor独立等到引擎current/pending均不再引用后销毁，Session关闭/析构也不破坏外部混合。

已编写：Core定义/字幕时序回归；CheckDialogue.ps1 + AetherDialogueCheck生产会话探针，覆盖真实Open、版本化Advance/Skip、菜单中断、注入伤害序号中断、目标版本变化、外部立即/混合镜头接管、真实Register提交后关闭仍持久成功、通道撤销恢复。前置使用服务器事实事务合成，伤害序号是受控fixture；不是实际GAS伤害、网络复制或分屏实测。既有联网范围内的拥有者隔离、Pawn替换、真实目标卸载、遮挡/视角质量仍需手工验收。所有新增用例均未运行。

相机API参考：[Epic SetViewTargetWithBlend](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/GameFramework/APlayerController/SetViewTargetWithBlend?application_version=5.5)。

分屏不是本Demo既定联机承诺；当前本机持久身份仍为LocalPlayer，不为本批探针改变身份规则。多拥有者隔离优先用两个独立客户端验证。
