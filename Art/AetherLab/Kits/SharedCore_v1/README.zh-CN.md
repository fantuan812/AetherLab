# SharedCore_v1 · 全场景素材规划与部分接口候选

2026-10-01。本批是完整需求矩阵（6区、12场景、25素材族、22 PROP）和9个母件的部分接口候选，不是完整逐Mesh成品库、全图建模或最终原画验收。套件门尚未整体通过。

## 打开与定位

完整恢复包保留仓库相对目录。解压后可直接打开 source/AetherLab_CoreKit_Interface_v1.blend；两张实际渲染在 previews/。本批二进制与图片通过用户的Library交付，GitHub提交保留文本规范、矩阵、重建脚本与验证记录；不把Library文件伪装成公开下载。

场景包含01母件、02共享网格试拼、03标注、04接口锚点、05现有角色审查副本。仅约35m×29m孤立试拼台，不是800m世界。

## 母件与实际优化

路石、矮墙、角柱、围栏梁、共享柱、灯柱沿用v7源的39个网格对象，求值后归并为6个母网格；另补最小路床，以及路石/路床的升坡派生，共9个。测试场34处实例共用9份网格。对象归并不等于GPU加速，也没有删除源场景。

4m为本批连接节距候选，净宽和视觉包围盒另论。石路母件保持石块形状比例，将原始石组XY统一缩小以适应4m路床；矮墙母件沿运行方向重整为4m。修改只在生产副本发生，原始源不变。未把整个世界或人物缩成统一包围盒。母材质复用源图，程序噪声改为明确的对象米尺度；图像材质UV保留。两张依赖图像均已内嵌。

## 尺度差异保留

v4主角设计全高1.65m；现有正式65骨baseline网格含发实测1.802796516m。试拼把165cm标尺和实际角色副本并排。副本只平移、替换中性审查材质，不改几何、拓扑、权重、骨架rest或pose。原角色源文件未改。旧1.65m审查相机的世界Z不是主角眼高。

## 验证范围

Blender 4.3.2重开后26项检查通过：10项锚点重合、3项共享柱计数、4项路床边界/标高、8项数据/角色/贴图/单位、1项81点主路中心线落点。9个角色对象另外与baseline逐项比对通过。见 docs/CoreKit_Validation.json、docs/Character_Preservation_Check.json。

不代表UV质量、法线质量、LOD、整面无缝、胶囊净空、碰撞、GPU性能或UE导航验收。未安装/运行UE，未编译、未测试玩法。道路端头、交叉修边、台阶、桥岸/路地/岩地过渡、其余建筑与玩法套件尚未闭环。

## 重建与独立检查

构建输入为既有v7完整场景 SCN01_Shelter_TA_Candidate.blend（Library libfile_7bdc97dcea808191af98e31eaff40bab，版本7）及 CHR_Peasant_Original65_Baseline.blend（Library libfile_d49623233fe88191b50a13860ee7cb46）。先按Library正规下载到本机，下面用实际本机路径替换。

```bash
blender -b /path/SCN01_Shelter_TA_Candidate.blend --python source/build_core_kit.py -- --character-source /path/CHR_Peasant_Original65_Baseline.blend --output-root /path/new-kit
blender -b /path/new-kit/source/AetherLab_CoreKit_Interface_v1.blend --python source/validate_core_kit.py -- --kit-root /path/new-kit
blender -b /path/new-kit/source/AetherLab_CoreKit_Interface_v1.blend --python source/verify_character_preserved.py -- --character-source /path/CHR_Peasant_Original65_Baseline.blend --output-report /path/new-kit/docs/Character_Preservation_Check.json
```

构建器只声明 awaiting_reopen_validation，不预置通过记录；独立验证成功后写 reopened_checks_complete。检查失败退出非0。脚本输出不包含原源文件或任何UE运行资源。

## 已确认Library交付

以 [Delivery_Manifest.json](docs/Delivery_Manifest.json) 的文件名、Library ID、版本0、字节数与SHA-256为准。完整恢复包为 `AetherLab_SceneAssets_CoreKit_v1.zip`，11564213字节（11.03MiB）。这些是已成功保存的私有Library文件标识，不是可公开访问的下载地址。取回恢复包后，source/和previews/二进制位于包内相同目录。
