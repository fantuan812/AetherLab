# 2026-09-27 复查整改执行与验证记录

依据：用户提供的《AetherLab 最新复查与修复方案》（2026-09-27）。此文件记录本轮实际代码、检查与证据，不把方案中的验证规格当作已经执行的测试。用户要求先完成全部修复，最后只做简单功能测试，且不编写测试代码。

## 代码工作单元

| 范围 | 提交 | 实际改动 | 当前状态 |
|---|---|---|---|
| RA-01 | `9c4c4f2` | 同伴控制显式进入/退出，仅清理组件持有的战斗状态 | `compile_passed`，定向规则覆盖有限 |
| RA-02 | `6b63d05` | 目标区域先加载，再按相关事务租约处理安全卸载 | `compile_passed`，跨区并发场景 `not_run` |
| RA-03 | `10e47fe` | 纯值精确转移计划与确认数量同源；整堆交换固定数量 | `compile_passed`，纯手柄落格 `not_run` |
| RA-05/06 | `1fb4114` | 详情行与按钮原位更新、图标请求缓存、快照内一次构建格位/实例索引及对象指纹 | `compile_passed`，刷新边界交互 `not_run` |
| RA-04 | `e731d3f` | 手柄选源、指定落格、取消与区域导航复用现有命令路径 | `compile_passed`，纯手柄实机 `not_run` |
| RA-07 | `0e4fb70` | 商店/库存/容器共用基础用途说明；已知资源与未知值分开；装备定义基础动作可见 | `compile_passed`，购买前后玩家对照 `not_run` |
| RA-08/09 | `22ebe51` | 场景动作按姿态及接触规则准入；资产完整语义对照；闪避延迟提交复验与共同规则时长 | `compile_passed`，弱网和取消矩阵 `not_run` |
| RA-10 | `2b14c0b` | 按配置/后端/结果隔离延迟样本；失败、无模型和冷启动留痕 | `compile_passed`，真实接触、画面和性能样本仍为 `insufficient_samples` |
| RA-12 | `88b47c7` | 打包脚本检查目标主程序、UAT Stage 清单、散装与 Pak/IoStore 推理载荷 | PowerShell 语法通过；正式候选 `blocked_engine` / `blocked_sdk` |

所有实现提交当时均注明“未编译、未测试”，没有在实现途中运行编译或测试。本轮未新增或改写测试源码，未修改已有资产；动作时长数值未改变，已有受控动作资源仍按原时长使用。用户原有的 `Config/DefaultEngine.ini` 工作区修改未纳入提交。

## 全部代码完成后的轻量校验

- `Scripts/Build.ps1`：首次链接失败，原因是新增快照索引方法缺少 Core 导出标记；同时 UE 5.8 没有 `EUINavigationRule::Automatic`，导航改用显式相邻控件，并修复局部名称遮蔽。修复后 `AetherLabEditor Win64 Development` 增量编译返回 `Result: Succeeded`。首次失败保留为历史结果，不视为通过。
- `Scripts/Validate/TestRules.ps1` 定向批次一：10 成功、0 失败、0 未运行。覆盖库存固定格与跨容器原子性、命令规范字节、闪避成本/碰撞/取消、同伴治疗规则和菜单生命周期。报告：`Saved/Automation/Rules-231a47c3c8ce4fd19044c4b73b3d55f5/index.json`。
- 定向批次二：10 成功、0 失败、0 未运行。覆盖 v1/v2 命令兼容、回执冲突、档案候选/重放、容器并发恢复与显式 DTO。报告：`Saved/Automation/Rules-fe7b9350078744ff8156fc6e632a1381/index.json`。
- `Scripts/Validate/TestMenuInteraction.ps1`：短启动页面交互与七张 1280×720 截图检查通过。日志：`Saved/Logs/AetherV10Menu_95410cade6ae.log`。
- `Scripts/Validate/TestAnimationPresentation.ps1 -Backend Traditional`：传统动画图检查通过，未报告动作集合资源失配。报告：`Saved/Automation/V10Pose_595112414cfc43d28d04c0205b0b14bb`。此结果不证明生成后端动作质量。
- `Scripts/Package/PackageClientServer.ps1`：PowerShell 解析通过；没有运行 Cook/Stage/打包。当前 UE 5.8 安装版存在 `InstalledBuild.txt`，Linux SDK 校验为无效，正式 Server Target 与 Linux 候选仍阻断。

上述规则测试不能代替 V01～V30 的完整玩家路径、跨区慢事务、纯手柄、弱网、接触质量、正式包与多人验收。未运行项保持 `not_run`；无有效接触样本时保持 `insufficient_samples`。未生成可发布候选，`releaseAccepted=false`。
