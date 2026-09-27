# v10 灰盒场景与正式旅程接线

此表记录生产定义与地图实例，不是运行验收。源为 `WorldObjects.json`、`Rules.json`、`Interactions.json`、`Items.json` 和正式页面；人物、地形、箱体继续使用 UE 官方模型及基础几何体。

| 玩家阶段或反应目标 | 现有物件/服务 | 正式入口与有限资源 | 已接生产边界 |
|---|---|---|---|
| 入城登记与教学 | Registrar/Register、Teacher/Teacher、Dummy | E 对话/登记、练习动作、技能页与任务页 | 个人 Claims/Evidence 和技能授予由持久命令、可信伤害事实判定 |
| 补给与整装 | SupplyA、SupplyB、Shop、Armorer、Well、个人/共享箱 | E 服务、背包/商店/双栏容器、有限库存与金币 | 实例 GUID、容量、资格、价格、回执由服务端重验 |
| 水/火 | ForestFire0～2、Bucket0～2、Well；LabFire/LabBucket/LabWell；FieldFire/FieldBucket | 有限桶水、法力与 Water.Draw；燃烧状态是目标，不靠播放特效判定 | 个人火盆只由真实灭火事件记事实；雨与电学含水量分开 |
| 支撑/通行 | WorksRope/WorksBridge、WorksWater0～2；LabRope/LabBridge/LabWater0～2；FieldRope/FieldBridge | 剑切或火烧绳索，水面可用霜凝；支撑关系与区域需求闭包共用 | 断裂/冻结为世界反应状态，传送等待结构及落脚碰撞 |
| 接触导电 | PowerSource、PowerReceiver、Crate；LabSource/LabReceiver/LabCrate；FieldSource/FieldReceiver/FieldCrate | 有限电源与导电物件接触，服务状态与物理状态一起保存 | 电源与接收端、反应容器、权威受电窗口不取决于视觉特效 |
| 日常/遭遇/结算 | Daily、DailyPatrol、DailyFire、AbbeyEntry、AbbeyValve、Activity、GuardianDefeated、Steward | 个人委托、组队入口、公共活动及个人领奖 | 同一任务目标 ID 供对话、HUD、日志、地图使用；奖励按个人持久身份结算 |
| 世界掉落 | 已提交掉落动态生成 Loot/战利品袋 | E 查看并从容器取物，显示内容摘要；袋是容器 | 材料仍保存在背包实例里，不暗示背包木材即独立可燃物理体 |

`Citizen` 有对白定义但地图没有对应实例，因此目前不会展示假对话。`Loot` 由已提交奖励动态生成，无固定地图物件。启动时生产校验检查目标锚点、静态服务实例和处理器；未知服务不能因有文案而自动得到执行权。

操作目录见 [默认键位](../Input-defaults.zh-CN.md)，实际重绑由本地设置覆盖。此表只说明现有场景的可达路径和设计的多解输入；组合、资源守恒、多人、重连与视觉接触均待授权后的运行验证。
