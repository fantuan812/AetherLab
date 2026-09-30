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

作者输入位于 `ContentSource/Equipment/WeaponGrips.json`，由 `Scripts/Authoring/WeaponGripBindings.py` 严格读取，只接受新完整契约，绑定原始握持 manifest 的 SHA-256。当前没有该输入，不提供虚构旋转或旧位置兼容转换。三个作者入口在首次修改资产前预校验所需全部行、真实来源文件哈希、已导入的身体/武器类型与完整路径、骨名/socket及参考姿态scale、既有输出资产类型。原始网格须先独立导入，不能借这个步骤推断导入轴。

schema 1 顶层字段只能是 `schema`、`coordinates`、`source`、`bindings`；`coordinates` 必须为 `unreal-local-centimeters-xyzw`。`source` 仅含合同目录内原始manifest的相对 `path` 与真实文件 `sha256`。每条 binding 必须包含：

- `item_id` 与完整包路径 `equipment_asset`（输出身份）；不同输出包可有同名物品，输出不能重复或大小写别名重复
- `target_mesh`、`weapon_mesh`（已经导入且身份准确的资产包）
- `main_bone`、`support_bone`、`socket`（socket 必须挂在声明的主手骨；直接使用该骨名表示明确的单位附着点）
- `weapon_to_socket` 与 `support_hand_to_weapon`；各自必须有三元 `translation_cm`、四元 `rotation_xyzw`、三元 `scale`

缺字段、未知字段、重复JSON key、位置旧格式、错单位/坐标标记、源哈希变化、异常旋转/scale均拒绝。作者写入新 `SupportHandTransform`、身体/手骨身份、`GripSourceSha256` 和显式已配置标志。运行时与作者使用一致数值约束；`SecondarySocket` 仍专供成对装备，双手武器不允许混用。数据预校验不声称能回滚磁盘故障或其他旧作者逻辑的失败；本轮三个入口均未在UE执行或改写二进制。

旧 `Tools/BlenderMCP/import_modular_unreal.py` 现在仅允许显式 `-AetherDataOnly` 进入该握持绑定流程。整批预检已经引用的身体/武器网格按完整包路径锁定，不得在同一次流程中 `replace_existing` 重导；资源缺失或对象发生变化即报错，没有缺失即导入的旁路。原始FBX先单独导入，核对实际导入坐标并建立真实合同后，再运行此绑定步骤。

确认需重作者化的双手资产：`/Game/AetherCore/Data/DA_TrainingHammer`、`DA_TideStaff`、`DA_BellHammer`，以及旧模块导入入口的 `/Game/SwordMagic/Data/DA_TrainingHammer`、`DA_BellHammer`。这是从源作者脚本确认的清单，不宣称读取了全部 uasset 序列化内容。它们缺完整副手朝向与身体身份绑定；基础几何版本还含非uniform scale，需烘入网格后提供新合同。其他二进制若包含双手配置，同样必须重作者化。旧二进制会明确失败，不能把本分支直接合入运行主线。

发布影响：`ValidateLoadout` 先要求整个 `Catalog::IsValidCatalog` 有效。因此任一旧双手资产缺合同会阻断整个装备目录的恢复/切换，也会影响单手装备与防具，并非仅关闭副手IK。必须一次备齐所用目录的全部真实完整合同和重新作者化资产后才能考虑合并；不能用fallback弱化门禁。

Quaternius 原65骨最新版 `.blend`、rest 合同、主副握点矩阵/手指姿态的原始 manifest 尚未恢复。展示视频不含这些可编辑数据。现有计划输出路径不是资源存在证明。

## 验证设计

1. 合成非共轴旋转 + 非零 socket 偏移，比较逐点坐标链和合成目标的位置/四元数；用 q 与 -q 验证同一朝向
2. component/骨架 uniform 100 倍与武器绝对 scale 分离；非uniform、反射、零scale、NaN和未归一化四元数全部拒绝
3. 缺输入、源哈希改变、重复绑定、目标身体/主副骨不一致、socket父骨不一致、装备变更/卸装后无陈旧握点
4. 世界接触优先，攻击跟随当帧主手，受击/死亡释放，传统/生成混合不改变 GAS 事件或模型输出权限

当前已实现源码、作者schema及测试源码，并执行Python AST解析与源码静态审阅；没有执行上述行为验证。`Aether.Equipment.FullGripTransform`、`Aether.Equipment.FullGripDefinition` 与 `Scripts/Tests/TestWeaponGripBindings.py` 已编写未运行。UE 验收本轮取消，后续也不自动安排。手指握姿仍沿用既有层，原骨手指资源接入是后续资源任务，不在本次通用副手变换实现中冒充完成。

参考API：[USkeletalMeshSocket](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/USkeletalMeshSocket) 返回socket局部transform；[USkinnedAsset::FindSocketInfo](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/USkinnedAsset/FindSocketInfo) 描述socket关联骨信息。空间链与缩放规则以项目现有 `AetherEquipmentVisuals::Attach` 的实际行为为依据。
