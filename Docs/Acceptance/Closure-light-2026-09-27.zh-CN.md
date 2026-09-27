# 121242c 闭环改动：简单功能测试记录

日期：2026-09-27。用户明确要求“运行进行简单功能测试”后执行。

## 环境与范围

- 工作区：`C:/ueproject/test`；分支：`main`；开始测试时 HEAD：`e017ec7a2eff3befcf7f320f658a321ecdebb947`。
- UE 实际版本：5.8.0，Changelist 55116800，`++UE5+Release-5.8`；Win64 Development Editor。
- 在现有工作区进行增量编译。保留了用户原有的 `Config/DefaultEngine.ini` 本地修改，未将其提交；本次不是冻结后的干净发布候选验证。
- 规则测试采用 NullRHI；短交互测试采用 D3D12 离屏渲染、1280×720、30 FPS 上限，每次使用独立的 `AetherV10Menu_*` 存档前缀。
- 键盘/手柄事件由检查程序送入 Enhanced Input / Slate；不代表实体手柄、完整主线或联网实玩验收。倒地和救起由受控状态建立，回据点经过正式 UI 与服务器恢复逻辑。

## 发现与修正

1. 首次编译失败：背包导航的 lambda 混合返回 `int32` 和 `INDEX_NONE` 枚举，MSVC 无法推导统一类型。已显式声明 `->int32`；随后增量编译通过。
2. 短检查最初在搜索焦点断言失败。诊断显示未触发筛选：UE 5.8 的 `UEditableTextBox::SetText` 不发出 `OnTextChanged`。检查改为经 Slate 文本变更进入正式 UMG 处理函数，并考虑搜索框内部编辑控件的焦点及下一帧更新；未为通过检查修改搜索业务行为。
3. 短流程断言通过后复核日志，发现恢复层启用了未配置动作的 CommonUI 默认返回绑定。关闭该多余绑定，保留正式预览事件对 Esc/B 的拦截；增加倒地层不可关闭的断言，并让脚本拒绝 CommonUI 绑定错误及 Ensure。

## 已完成的规则检查

`Scripts/Validate/TestRules.ps1` 的新报告 `Saved/Automation/Rules-c2a47518f6754896b810a0e20940983b/index.json`：6 项成功、0 警告成功、0 失败、0 未运行、0 进行中。

| 用例 | 覆盖内容 | 结果 |
|---|---|---|
| Aether.V10.Closure.UnsentInventoryIntent | 未发送背包意图的身份、格位、内容与容器授权复核 | 通过 |
| Aether.V10.Closure.DetailPressIdentity | 无关刷新保留按压；换对象、禁用后恢复取消旧按压 | 通过 |
| Aether.V10.Movement.CapsuleHeadroomJumpAndSprintPrediction | 胶囊低顶、跳跃、冲刺预测；保持与切换蹲姿共同作用及生命周期清理 | 通过 |
| Aether.V10.Movement.GASGroundDodgeCostCollisionAndCancel | 闪避消耗、碰撞及取消 | 通过 |
| Aether.V10.Consumables.RollbackLostReplyAndRespawn | 消耗品回滚、丢回复及重生相关规则 | 通过 |
| Aether.V10.Modules.ClientPresentation | 客户端 UI 模块及旧类重定向 | 通过 |

报告契约检查：有效夹具被接受，11 个格式错误或不匹配夹具被拒绝。相关 PowerShell 脚本语法检查通过。这两项不构成玩法验收。

## 最终短流程结果

最终增量编译通过，记录：`Saved/Acceptance/ClosureLight/build-final.log`。此前的 6 项规则用例及其被测规则实现没有随后改动，因此没有重复运行无关规则；最后一次重新编译与短流程覆盖恢复层及检查程序的最终修正。

`Scripts/Validate/TestMenuInteraction.ps1 -ClosureLight` 通过：37 条菜单 PASS、18 条专项 PASS，日志中 0 条 Error / Fatal / Ensure。正式菜单的打开、切页、关闭、Pawn 替换与键盘/手柄菜单入口均经过检查；专项覆盖：

- LB 单独及 LB+十字键无下蹲/基础技能副作用；LB+B 切换蹲姿且不扣闪避耐力；普通 B 仍触发闪避。
- 无匹配筛选将全部格子排除，并把焦点从原格子转移到搜索框内部编辑控件。
- 倒地等待层、提前恢复被阻止、Esc/B 不可关闭、Y 经 Slate 返回据点、新 Pawn 恢复游戏输入。
- 重复确认、旧层迟到回调、第二次倒地及受控救起后的层关闭。

最终日志：`Saved/Logs/AetherV10Menu_ddac02ad52b8.log`；入口结果：`Saved/Acceptance/ClosureLight/menu-final.log`；截图目录：`Saved/Automation/AetherV10Menu_ddac02ad52b8/`。8 张 PNG 均由脚本验证为本次新生成、1280×720；已检查菜单截图及最终背包/倒地画面，倒地层显示等待秒数和 Y 提示，等待期间回据点按钮禁用。

前期失败日志及含绑定错误的中间日志保留，不作为最终通过证据。截图仍是 UE 官方角色和占位场景；动作数据包提示未部署，不属于本次验收范围。

## 复现入口

```powershell
& Scripts/Build.ps1
& Scripts/Validate/TestRules.ps1 -Filter 'Aether.V10.Closure.+Aether.V10.Movement.CapsuleHeadroomJumpAndSprintPrediction+Aether.V10.Movement.GASGroundDodgeCostCollisionAndCancel+Aether.V10.Modules.ClientPresentation+Aether.V10.Consumables.RollbackLostReplyAndRespawn'
& Scripts/Validate/TestClosureReportContract.ps1
& Scripts/Validate/TestMenuInteraction.ps1 -ClosureLight
```

`-ClosureLight` 使用原菜单短检查及本次专项检查，跳过默认 100 次菜单循环；每次新建独立日志、截图目录和存档前缀，检查进程结果、成功标记、错误标记及 PNG 时间/尺寸。

## 验收边界

本次不运行多人压力、长循环、完整 Cook 或正式包制作。键鼠/实体手柄完整主线、弱网重连及服务器恢复组合、动作质量与性能矩阵、Client/Win64 Server/Linux Server 正式包和许可证检查仍未验收。`releaseAccepted=false`。

原始日志、自动化报告、截图和隔离存档保留在本机 `Saved` 下，不进入 Git。前期实施记录的“未编译、未测试”是历史状态；本文件只更新上述轻量范围，不能把局部通过扩展为整个执行方案验收完成。
