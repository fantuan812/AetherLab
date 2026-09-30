# 完整双手握持变换契约

状态：独立工作分支上的实现检查点；未编译、未运行规则测试或 UE，禁止合并至运行主线，直到原始握持合同、对应资产和静态复审齐备。Quaternius65 保持 draft，不替换骨架或猜测导入轴。

## 已确认缺口

- `SupportHandOffset` 只保存主手骨局部位置，副手 IK 未消费朝向
- `AetherEquipmentComponent` 额外按固定轴旋转武器，而副手目标没有同一变换
- `PrepareV10Equipment.py` 为双手资产写入 24/32cm 常量，无法重建副手旋转
- 现有闭合手指来自轻击的固定采样时刻；真正原骨手指姿态仍需恢复素材合同后单独接入

## 唯一空间语义

- 主握持 `GripTransform`：武器模型局部 → 主手附着 socket 局部
- `SupportHandTransform`：副手骨局部 → 武器模型局部，包含位置和单位四元数，scale 必须为 (1,1,1)
- 契约绑定完整目标 SkeletalMesh 身份、武器 StaticMesh 身份、主/副手骨名与 socket；不能跨身体复用旋转
- 当前帧按 主手骨 component pose → component world → socket local → weapon local → 副手握点 求值，再回到 component space。只使用同次图评估的主手骨，不读上一帧武器世界坐标
- 武器继承 socket 位置/旋转，但 world scale 是 `GripTransform` 的绝对 scale。骨架的 100 倍单位 scale 不得乘到武器几何上；socket 局部平移和武器挂载平移仍按真实附着语义处理
- 所有参与求值的 transform 必须有限、旋转归一化，scale 正且 uniform；副手握点必须刚体。非uniform/负/零 scale 明确拒绝，不能丢弃 scale 后静默当刚体
- 同一 IK 输出同时应用副手位置与旋转；正常 skeletal-control alpha 对两者共同混合。主手正式动画轨迹不被额外固定轴摆动覆写

## 作者与迁移边界

作者输入计划位于 `ContentSource/Equipment/WeaponGrips.json`，只接受新完整契约，绑定原始握持 manifest 的 SHA-256。当前没有该输入，不提供虚构旋转或旧位置兼容转换。所有作者入口必须在修改资产前预校验需要的全部行。

确认需重作者化的双手资产：`/Game/AetherCore/Data/DA_TrainingHammer`、`DA_TideStaff`、`DA_BellHammer`。这是从源作者脚本确认的清单，不宣称读取了全部 uasset 序列化内容。它们缺完整副手朝向与身体身份绑定；基础几何版本还含非uniform scale，需烘入网格后提供新合同。其他二进制若包含双手配置，同样必须重作者化。

Quaternius 原65骨最新版 `.blend`、rest 合同、主副握点矩阵/手指姿态的原始 manifest 尚未恢复。展示视频不含这些可编辑数据。现有计划输出路径不是资源存在证明。

## 验证设计

1. 合成非共轴旋转 + 非零 socket 偏移，比较逐点坐标链和合成目标的位置/四元数；用 q 与 -q 验证同一朝向
2. component/骨架 uniform 100 倍与武器绝对 scale 分离；非uniform、反射、零scale、NaN和未归一化四元数全部拒绝
3. 缺输入、源哈希改变、重复绑定、目标身体/主副骨不一致、socket父骨不一致、装备变更/卸装后无陈旧握点
4. 世界接触优先，攻击跟随当帧主手，受击/死亡释放，传统/生成混合不改变 GAS 事件或模型输出权限

当前仅恢复与审阅源码，未把设计验证标记为已执行。UE 验收本轮取消，后续也不自动安排。
