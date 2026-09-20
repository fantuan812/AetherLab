# v10 装备属性与外观发布

本单元未编译、未测试；静态核对本机 UE 5.8 GAS 接口并运行 git diff --check。

已提交 Profile 通过统一 EquippedStats 计算器产生十种加成，持续 ASC 上只维护一个 UAetherEquipmentEffect 来源句柄；同值重发不更新，变更用 SetByCaller 更新，源效果被清除后可重建。角色上限降低夹取当前资源，上升不回血。

武器 Damage/Posture 加入实际 EquipmentHit；物理伤害按 100/(100+Armor) 结算（Armor 上限 900），元素伤害按百分比抗性减免（最多 80%）。热暴露和电窗口使用各自伤害类型，冰冻减速读取 FrostResist。仅调整角色结算，不修改反应求解器的水热电守恒状态。Water/Frost 类型可供对应伤害调用；当前内容没有水伤害物品，不声明已有水伤害玩法。

外观投影保存原生实例 GUID、明确选中槽位；组件支持十槽、AllowedSlots、戒指双槽和成对防具骨骼。重复 loadout 不取消攻击/重建外观。细小饰品允许无世界网格。原生流程拒绝旧 Profile 恢复覆盖。

PrepareV10Equipment.py 编写了七类防具/饰品资产制作流程，加入现有官方基础目录；尚未运行，uasset 尚未生成。正常 GameMode 激活、耐久磨损的持久事务、视觉资源制作和双人物检视仍需完成。本记录不是 V10-07 完成或验收。
