# CHR01 165cm 主角七武器：Blender 源合同与限定范围量测

## 本交付是什么

这是银发 CHR01 主角的七件武器、六段可编辑握持场景：剑＋盾、双手法杖、锤、矛、大剑和动态弓。仓库只保存精炼源合同、证据摘要和只读复测脚本；`.blend`、贴图、图像与视频单独保存在用户 Library。它不是 UE 可直接导入资产、运行时握持代码或全身零穿插验收。

- [交付清单与 Library 身份](../../ContentSource/Characters/CHR01/WeaponGrips/manifest.json)
- [冻结角色身份](../../ContentSource/Characters/CHR01/WeaponGrips/contracts/FrozenCharacter.json)
- [固定握持量测摘要](../../ContentSource/Characters/CHR01/WeaponGrips/evidence/FixedGripReview.json)
- [动态弓 v3 量测摘要](../../ContentSource/Characters/CHR01/WeaponGrips/evidence/DynamicBowV3Review.json)
- [本批只读复测实跑结果](../../ContentSource/Characters/CHR01/WeaponGrips/evidence/ReproductionCheck.json)

三批 Library 回执已确认：第一批源包v2、第二批源包v1、第三批弓源包v0；全部17项源/贴图/预览文件的身份、版本、字节数和SHA见清单。文本包冻结待审查，不代表已发布PR。Library ID 不赋予公共仓库读者下载权限，也不在仓库存放临时签名下载链接。

公开摘录中的作者机器绝对路径已统一为 `hero_weapon_grips` 素材工作根目录相对路径，仅45个路径字符串规范化；数值、源报告SHA及字节数不变。源SHA仍识别未改写的原件，不能用规范化摘录重新计算来替代。完整原件的Library包ID/版本与ZIP内路径见清单 `provenance_archive_map`；13项未归档原件明确标为仅作者本地，不承诺可下载。

## 冻结与副本边界

原 `CHR01_Wanderer_Editable.blend` SHA256：

`b227a2d002aed8724acdfd3f0d1a21aef5652f486b63245ed1278fe96a3eba69`

身高 `1.650740146636963 m`，骨架 `CHR01_Wanderer_Rig65`，65 骨。原源合同 rest SHA256：

`fa3ec5e21ee097e021fdae91d009bb71269beb0d1e3d8d2a0a21cba1a19afdf0`

原文件、原 Base/Basis、原网格/权重、骨 rest 和已有 shape 坐标保持；衣物修正只在另有 SHA 的演示副本新增局部 shape keys。数组格式的 rest 指纹 `fa402e…` 使用另一种序列化，与上面的源合同 JSON 指纹不同，不能混作哈希不一致。

固定五模式的当前外置文件 SHA 在各合同的 `derived_editable_sha256`。动态弓分开保留三种身份：

1. 握持基线：`90fcb8bb2f1e3596efbd3a16bdef79fd95caa1ef975d9b1121c286e5e7a8bbcb`
2. packed 衣物 v3：`e9e31ed433181f8cc49fad36d3d3e0cc14e1f88e165ef7b8a14b780dd072500e`
3. 最终外置弓：`16638c8b3a7291d08e374ea559a571f95553eadd92def24988c3564062a83ad5`

旧弓基线报告中的“未校正披肩”描述属于基线，不能移植成 v3 的当前状态，也不能将旧候选的否决当作 v3 的结论。

## 坐标、矩阵与四元数

所有数值坐标为 Blender 右手系、米制、`+Z` 向上，角色面向 `-Y`。矩阵按行序列化，作用于列向量；JSON 数组显示的行顺序不代表改成行向量乘法。

设 `A` 为 armature-to-world，`H` 为 posed-hand-to-armature，`W` 为 weapon-to-world：

- `weapon_local_to_hand_local = inverse(A @ H_main) @ W`
- 因此 `W = A @ H_main @ weapon_local_to_hand_local`
- `support_hand_local_to_weapon_local = inverse(W) @ A @ H_support`
- 因此 `A @ H_support = W @ support_hand_local_to_weapon_local`
- 接触框：`T_world_contact = A @ H @ contact_frame_hand_local`

主/副握点都是完整位置与旋转关系，不能简化为一个位置 offset。固定双手关系先解析求解，再逐整数帧密集烘焙；交付的手臂没有 live IK。

`rotation_xyzw` 和仓库归一化后的 `finger_pose_quaternion_xyzw` 顺序为 `[x,y,z,w]`。原始手指校准报告字段是 `finger_pose_quaternion_wxyz`，按 Blender 原生顺序 `[w,x,y,z]`；原数组和报告 SHA 未改。归一化合同只做可逆分量重排，写入 Blender `rotation_quaternion` 时必须转回 WXYZ，不是旋转轴变换。

28/32mm 手校准夹具的局部 `+X` 为指端方向、`+Z` 为朝拇指侧的圆柱纵轴，`+Y` 完成右手基；动态弓局部 `+X` 为飞行方向、`+Z` 向上。它们不是 UE 导入轴映射。

## 动作范围

六段均为帧 `1–97`、24fps。固定模式源报告含 97 整帧姿态，绑定/表面主要量测为 193 半帧采样。精炼合同删去庞大的 dense samples/key-poses，原完整合同由 SHA 定位并随 Library 源包保留。

弓单独采用 [DynamicBowV3.json](../../ContentSource/Characters/CHR01/WeaponGrips/contracts/DynamicBowV3.json)：左手握弓、右手拉弦，`support_hand_fixed_transform = null`。

- 1–61：右手跟随变化的弦 nock；61 后解除加载接触
- 61–63：食/中/无名指打开，右腕沿弓 `-X` 后撤约25mm
- 63–69：弦与弓肢回位；小指折曲保留至69，之后放松至81
- 手臂与手指逐整帧烘焙；弦端使用独立 helper 约束/长度 driver，以维持子帧端点关系

弓作者表面量测241时刻；独立姿态/端点量测245时刻，实际 glove↔可见弓网格（含弦管）三角交叉检查24关键时刻。三个采样数对应不同检查，不能互换。

## 已确认量测与已知限制

- 28/32mm：每手15416表面点，1467选定手部三角形对无限光滑圆柱的径向解析检查无侵入。只检这些手面和无限柱，不包含端盖、护手、全身或连续运动
- 剑盾/法杖：193半帧整件低模披肩↔左右袖 BVH 相交为0。新版驱动依赖刷新用193时刻可见几何/骨/对象矩阵逐位等价继承前版量测和预览，没有把旧图像冒称从新 SHA 重渲
- 锤：握持量测通过，但隐藏右肩袖面仍在披肩壳内。第73帧独立代表点最近面距离约 `0.555442 mm`，6方向奇偶检查确认内部；不能称衣物全部通过
- 矛/大剑：指定后肩内域检查通过，仍有臂孔/边缘三角交叉。大剑支持腕 basis 峰值 `20.766565°`，不满足严格≤20°口径
- 弓 v3：指定后肩内域193半帧可见露袖0；同期仍有95个采样时刻、212条隐藏壳内射线命中。第35.5帧代表点最近面距离 `0.288275 mm`；后向最小净隙 `−17.811093 mm` 是射线轴向量，不是欧氏穿入深度或全片最大穿深
- 弓还有前肩/臂孔可见蓝袖残留：最终右掌49近景确认可见，原握持基线同样存在，不在后肩内域检查范围内；相机射线约9.609mm层间距离也不是欧氏穿深。不能把所有剩余问题概括成隐藏碰撞
- 弓手弦241作者样本与基线逐值全等，右手弦表面最小采样净隙 `+0.244997 mm`；独立24关键时刻 glove↔弓/弦管三角交叉0。有限采样不证明扫掠连续无穿透
- 弦到 helper 端点最大误差 `0.000492415 mm`，实际木弓末端截面中心最大偏移 `0.300824644 mm`，必须分开。独立左右腕 basis 峰值约 `22.2043°/24.0249°`，不是全片严格小于20°，也不是医学关节角限位

衣物新增 key 的实测同帧原 Idle→Grip→None 隔离通过；Blender 4.3.2 仍有 action 属性依赖注册警告，不能宣称所有版本、任意 NLA 混合都已验证。弓新增阶段披肩形态最大原位移35.50002mm是造型调整幅度，不是净空。

上述是现有 Blender 量测与独立复核的具体结论。接触框闭合、可见蓝斑消失和衣物全表面净空是三件不同的事。没有力学拉力标定、全身连续碰撞证明或全角度服装验收。

最终弓媒体独立核对17张PNG、1个MP4及33张源帧哈希，视频完整解码通过：640×720、8fps、33帧、4.125秒。人工查看的是报告列出的6张最终静图和4张抽帧，不能写成33帧全部人工审阅；媒体完整性不改变上述衣物限制。

## 解包与只读复测

从清单确认 Library 文件身份、版本和 SHA，再把三批源包和相应贴图包解到同一工作目录；保留源包中的 `source/` 与 `source/textures/` 相对路径，勿从仓库猜下载地址。第一批源包必须使用清单所列的新版本，不能只按同名文件选择旧版本。

离线文本检查（不运行 Blender 或 UE）：

```sh
python ContentSource/Characters/CHR01/WeaponGrips/scripts/verify_text_bundle.py
python ContentSource/Characters/CHR01/WeaponGrips/scripts/verify_text_bundle.py --asset-root /path/to/unpacked
```

Blender 只读有限复测：

```sh
blender -b --python ContentSource/Characters/CHR01/WeaponGrips/scripts/verify_blender_bindings.py -- --asset-root /path/to/unpacked --mode Staff
blender -b --python ContentSource/Characters/CHR01/WeaponGrips/scripts/verify_blender_bindings.py -- --asset-root /path/to/unpacked --mode DynamicBow
```

脚本先核对精确场景 SHA、65骨 rest、米制和帧范围，不保存 `.blend`。固定模式复测193半帧主/副相对绑定误差；动态弓复测245时刻主接触原点、helper 端点与实际弓肢截面偏移。脚本不重跑全部手部/衣物碰撞审查，不把它的输出写成全套通过。现有全部碰撞范围与失败项以证据摘要及原报告为准。

本批已在六份精确SHA场景上实际执行上述脚本：5×193固定样本与245弓样本，rest及文件哈希不变。固定平移残差逐值重现，SVD/atan2给出小的非零角误差（最大0.009437°）；部分历史报告的0°值保留作原始记录，不外推成绝对零旋转漂移。这里不是未运行的复测建议。

## UE 与 PR13

UE 验收已由用户取消，不是后续待办。本批未运行 UE 编译、规则/行为测试、启动、Cook 或 UE 验收；不得把此前完成的 Blender 量测也概括成“未测试”。所有实际 UE 目标 mesh、武器 mesh、装备/动画资产和导入轴字段保持 `null`，无虚构资源路径，无可直接导入承诺。

[PR13](https://github.com/fantuan812/AetherLab/pull/13) 缺真实目标/导入信息和完整运行资产迁移，继续保持独立未合并。本源合同 PR 不复制其运行时代码，不为它补猜测值，也不以相同65骨数量声称身体身份兼容。
