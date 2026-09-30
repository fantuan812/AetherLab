# MotionBricks 原65骨适配基础

状态：本单元完成源码及作者契约，**Quaternius 原生资源尚未生成，不能宣称运行适配已接通**。用户已取消本轮 UE 验收；本单元不安排 UE 编译、运行、安装或 Cook。下述静态检查不能替代未发生的 UE 加载。

## 一份当前绑定目录

`Content/AetherCore/Definitions/MotionBindings.json` 是作者工具与运行时共享的权威目录。版本只接受 schema 1；运行时进程首次读取后保持不可变，编辑目录需要重启，不做部分热重载。字段大小写错误、未知字段、重复骨架/目标网格/输出资产或角色绑定失败时，目录整体拒绝；没有名称猜测、备用骨架或旧格式转换。

- Manny / Quinn：`configured`，分别以现有 `SKM_Manny_Simple` / `SKM_Quinn_Simple` 的完整对象路径索引。它们仍使用仓库现有 MotionProfile、G1 Retargeter、动画蓝图、传统片段与受控动作目录；没有新增必须填入旧二进制的 CharacterDefinition / Profile 字段
- Quaternius65：`draft`，原生网格包路径、导入后朝向和传统动画片段绑定均未确认，明确为空；运行时不索引草案，作者显式请求该条目会报错。计划输出路径不是已存在资源的声明
- 完整路径按 UE SoftObjectPath 资产身份比较和排重（大小写别名不是第二份资产），不同目录内同名 Manny 网格不会命中。网格变化使旧异步请求和生成姿态失效，然后按新完整路径选择；缺少绑定时停用生成动作并保留具体诊断
- 传统动画同样按该目录选择。资源在原生图代理初始化节点前加载，检查片段/BlendSpace 与当前身体 Skeleton 一致。缺资源或不同 Skeleton 不借用 Manny 片段。此处保留合法的动作资源→动画图接口，没有增加一个旧格式适配层
- 角色作者、预览作者、动画蓝图作者共用目录中的资源引用。生成的角色资产保持现有格式；运行时不会反推字符串中的 Manny / Quinn 名称

## 原骨链与坐标

原骨骼保持 65 根，不加 Manny 辅助骨、不改推理模型。实际名称是 `Head`、`spine_01` 至 `spine_03`、`hand_l/r`、`foot_l/r`；没有 `spine_05`。已对照角色作者提供的原骨 rest 合同核对全部目标链的祖先关系，G1 仍为锁定的 34 骨。

作者工具从目录读取双方根骨、脊椎链、肩/肘/腕、髋/膝/踝、趾链和参考轴对齐点。维持关节分段映射，避免把 G1 多关节整臂/整腿插值成错误的人体屈曲。骨骼缺失、链端非起点后代或目标网格不是声明包均拒绝。共享 G1 Rig 的定义必须一致，且任何源 Rig 路径不得与任一目标 Rig、Retargeter 或 Profile 输出重合，作者 mutation 前也拒绝源/目标为同一对象；更新既有链时不删除再重建同名链，避免清掉另一身体的映射。

Blender 合同已知：米制、Z-up、角色朝 -Y、Armature scale 为 (1,1,1)。这些是源几何信息，**不等于 UE 导入后轴向**。`source_heading_degrees` 因此仍为空。素材作者交付的 `Quaternius_Weapon_Grip_Proof/grip_manifest.json` 保存完整 rest 矩阵、主副手握点矩阵和手指姿态；本目录不复制一套可编辑握点。该交付与仓库中的 UE 原生资产是不同阶段。

## 武器与 GAS 边界

独立武器仍走当前装备槽和静态网格挂载；主手握点必须取原65骨的手骨局部合同，不能复用 Manny 旋转值。双手武器的副握点包含位置与朝向，素材证明的武器局部 40 cm 位置不应被误写成“手骨局部 40 cm”。现有 `SupportHandOffset` 只有主手骨局部位置，尚不能表达完整副手朝向；这部分原生表现绑定仍待实现，不能把 Blender 持握证明当作游戏里已经接通。`SecondarySocket` 当前用于成对装备，也不能充当副手握点。

本单元不扩大生成动作权限。攻击、施法、格挡、闪避、翻越、空中和其他受控动作继续由既有 GAS / Actions 时序掌管，`AllowsGeneratedMotion` 门控未变；`Combat` 风格表示战斗移动，不授予生成攻击命中权。后续若开放生成攻击，须另有阶段、接触、握持和 GAS 事件契约，不能由模型输出直接决定伤害或物品提交。

## 现有资源及打包依赖

静态检查脚本：`Scripts/Validate/InspectMotionBindings.py`。它检查 configured 行引用的 uasset 文件、G1 源骨链，并可接受角色作者的 `--native-contract` 核对原65骨链，不会加载 Unreal。

当前 Manny / Quinn 的 45 次包引用检查均发现对应文件（包含复用资源，不代表 45 个独立资源）。现有 `/Game/AetherCore` 与 `/Game/Characters/Mannequins` Cook 根保留，新增 `/Game/Animation` 根覆盖从构造硬引用改为目录软路径后的动画/Retargeter/Profile；Definitions 的既有 NonUFS 规则覆盖目录。无需先重生成资产标签才能把本次使用的现有资源纳入配置。作者流程同时将 configured 行资源写入现有 `DA_MotionCook`，单独生成一种身体不删除另一种依赖。尚未执行 Cook，不把配置覆盖当作 Cook 通过。

## 检查记录与剩余交付

已执行：Python 作者脚本 AST 源检查、目录当前数据及 45 次包文件存在检查、G1 34 骨与 Quaternius65 rest 合同祖先链检查、Blender 只读骨表/单位/对象缩放检查、git diff 空白检查。没有修改角色作者文件。

已编写未运行：`Aether.Motion.BindingContract`，覆盖当前目录、两种既有身体、草案排除、同名不同目录拒绝、字段大小写、版本、重复网格、源/目标路径碰撞（含大小写别名）及失败不保留旧结果。

未生成：Quaternius UE SkeletalMesh / Skeleton、IKRig、双向 Retargeter、MotionProfile、原骨动画蓝图、传统/受控动画及原骨武器表现绑定。目标网格/导入轴向未确认前不得把 draft 改 configured。现有资源未被本单元修改，当前二进制并不因为有 JSON 草案而新增任何原65骨能力。以上是必要内容交付的真实缺口，用户取消 UE 验收并没有消除这些缺口。
