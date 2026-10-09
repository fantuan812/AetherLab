# KITE-01：原创虚构模块化步枪美术首版

用途：AetherLab 游戏可视化资产和模块化美术管线试件。仅有外观，没有真实武器内部机构、制造公差、功能性接口或现实装配说明。角色尺度属于游戏空间约定。

## 当前成果

- 可编辑原生 `KITE01_Modular.blend`，Blender 4.3.2 实际执行生成和重新打开
- 9 个基础外观/活动模块：Receiver、Handguard_Sand、Muzzle_Short、Stock_Skeleton、Grip_Angled、Magazine_Box、Optic_Reflex、ChargingHandle、Trigger
- 2 个真正独立替换外观：Stock_Compact、Muzzle_Cover；替换件与对应默认件共享接口枢轴。默认在 VARIANTS_hidden 集合中隐藏，使用时只能显示同槽位的一件
- LOD0/1/2：26612 / 12768 / 5314 三角；3 个分级 GLB，11 个逐模块 FBX
- UV0_Surface（替换件可能保持 UVMap 名称）、Principled PBR、512×512 自制基础色与粗糙度纹理；金属度为材质标量。未制作高模烘焙法线/独特磨损图集或第二套烘焙光照UV
- 8 个挂点/目标：主手、副手、弹匣、瞄具、枪托、枪口、瞄准、换弹空间。模块插槽本地轴已统一；手部挂点尚属适配候选，不能把初始 identity 朝向当作已验收握持合同
- 24fps、1–96帧物件级弹匣移出/回位与外部拉柄运动研究；扳机独立并有枢轴，但未制作扳机动作。不是完整角色装填、开火或联网装备系统

## 图像与风格范围

先生成 reference/KITE01_Concept.png，再制作真实几何。renders/KITE01_Beauty.png 与 Side 为真实 Blender 渲染。
参考图的目标更写实，表面磨损、护木凹槽和机匣轮廓更精细。首版保持识别轮廓、沙色/石墨色搭配及模块层次，但目前较方正，护木槽为浅表外观饰板，尚未达到参考图细节质量。不能把概念图冒称模型渲染，也不把此首版称作最终 AAA 资产。

## 场景、轴与导出

Blender 右手米制，+X 前向、+Z 上向；KITE01_Root 为整枪原点。所有三维数字只用于游戏物件的尺寸/动画，不是现实制造参数。LOD 共用原点和装配姿态。

GLB 包含 PBR 与 LOD0 的物件动画；LOD1/2 静态。FBX 每件在该模块本地接口原点独立导出，使用 Blender 4.3.2 FBX 7400 writer，前轴 -Y、上轴 Z。FBX 不包含整体装配转换与多个对象 socket 自动导入；按 Build_Manifest 的 pivot 重建视觉组合，socket 权威数据在 GLB/JSON。Epic 当前文档注明 FBX 2020.2，因此这里不能保证 Blender FBX 版本兼容特定 UE 导入。没有 UE 资产路径，也不声称可以直接无调整进入引擎。

## 已执行验证

`source/validate_kite01.py` 实际重新打开原生文件、逐件检查封闭网格/有限坐标/正尺度/UV0范围、检查纹理可用性及8挂点；重新导入全部3个GLB和11个FBX，核网格数量、三角数、轴向包围盒、动画存在与FBX原点。

共154个限定结构检查通过，具体见 docs/Blender_Validation.json。UV范围通过不等于逐像素texel密度、重叠或高模法线烘焙验收；闭合独立壳并不意味着模块彼此无相交。没有全角色/手部穿插、连续换弹扫掠或肩部碰撞通过结论。

UE 验收已由用户取消，未运行 UE 编译、规则/行为测试、启动、Cook 或验收。Blender 检查已经执行，不能笼统写成所有内容未测试。

## 角色来源边界

最新 main 冻结的是 CHR01_Wanderer_Rig65、65骨、1.650740146636963m角色，不是旧 Peasant。冻结原 .blend 预期 SHA b227a2d002aed8724acdfd3f0d1a21aef5652f486b63245ed1278fe96a3eba69，本轮没有取得该原文件；当前 Library 原包已到v3，不能冒作冻结v2。

本轮取得 main 精确 SHA 对应的 Stage1 v2 派生握持场景作为参考，并实测骨rest与冻结合同逐骨一致。派生源文件不覆盖、不改变。角色适配研究与其具体限制将单独记录，不把合同框架闭合等同手掌表面无穿插。

## 可复现

blender -b --python source/build_kite01.py
blender -b --python source/validate_kite01.py
blender -b --python source/render_kite01.py
blender -b --python source/render_modules.py

build 会覆盖此生成目录的同名输出，需在拷贝目录重建，避免覆盖手工修改文件。仅依赖 Blender 自带 bpy/mathutils/numpy，不访问外网。原角色副本适配另有其输入路径，不是此独立枪体重建前置条件。

## 后续资产顺序

先在同一虚构平台增加短/标准/长护木、枪托、弹匣和瞄具外观，验证槽位兼容、库存图标、拾取/持枪/换弹动作；然后逐平台补充手枪、冲锋枪、霰弹枪、精确步枪。每个平台需要自身装填空间/手部接触和活动件，不能只把同一枪按比例缩放。

## 持久交付与仓库边界

仓库保存可复现脚本、部件/挂点数据、验证摘要、来源和交付身份；不把本机工作区当作用户可下载地址。模型、纹理、渲染及完整打包导出持久保存在用户 Library：

- 独立原生模型 KITE01_Modular.blend：libfile_d432e9b55ac88191af10b7d6e354ce41
- 枪体完整包 KITE01_WeaponAsset_v1.zip：libfile_da19234fb2788191b7199dbc7af2fd1e（具体当前版本、SHA见单独 Delivery_Manifest.json）
- 参考图：libfile_866b5737c9e88191ad8d09f7a0d70c85
- 实际Beauty渲染：libfile_76db3bc4df608191bf1c0523ac11ae2c
- 实际侧视：libfile_6c6832006d888191819df93ec46beaad
- 部件总览：libfile_1ed282e52cb0819199981ac9b79dd950

Library身份不赋予公共仓库读者访问权，交付通过用户自己的附件入口取用，不发布临时下载链接。武器包包含原生模型、纹理、11FBX/3GLB、图像、脚本和自校验File_Index，角色适配另包且明确为研究示意。

## 改装/动画缺项，不在本批冒充完成

尚未制作：短/长护木互换实测、不同弹匣/瞄具造型、完整部件兼容规则和库存图标、碰撞壳、第一人称专用网格、扳机动作、开火/瞄准/检查/装填失败等角色动作、手指逐面贴合、抵肩接触、镜头/反冲动画、动作混合、装备逻辑与网络同步。武器外观槽位存在不代表这些游戏系统已实现。现角色图只验证静态尺度与目标框姿态，未完成角色换弹动画。
