# v3 最小地形连接批次独立审查

## 结论

**通过本批次的 Blender 静态几何与五视图复核，未发现要求返修的阻断缺陷。**

冻结源：`deliverables/source/AetherLab_CoreKit_Transitions_v3.blend`

SHA256：`46fc07828d7b3a19d7362f41e0cf03da79df321ea92172fa421f8b6b841767b3`

只验收 `06_CONNECTED_TRANSITION_FIXTURE__NOT_WORLD` 的真实连接样段与 `05_TRANSITION_MASTERS__CANDIDATES` 的六个新增母件。旧 v2 展示组均隐藏。审查没有修改作者 build 脚本或保存/改写 blend。

## 独立性与覆盖

重新打开实际 blend，依据评估网格、世界变换、BVH 射线、底面/体内点采样检查；没有执行作者验证函数，也没有拿预设摆放坐标自比较。角色另从 v2 与 v3 两份实际文件独立提取指纹比较。Sockets 是待审查的元数据，其空间意义另与真实局部网格比较。

- 新接口元数据：六母件及其25个实例全部只保留统一新字段，无旧 `grid_m`、`connectors_json`、`ROAD`、`EARTH` 等冲突键；实例字段与对应母件一致
- 六个母件：均闭合、正体积，无非流形边、错误绕向、零长边、退化面/三角形；SurfaceUV 坐标有限且所有三角形 UV 面积非零
- 六母件的 25 个新增实例及样段全部 77 个实例：真实共享母件 mesh datablock，实例缩放均为 1；新增母件材质复用原有 basalt/earth/cedar datablock
- 台阶：实际采出八个踏面与八次约 0.125m 上升；下口无水平裂缝，上口同高误差 <0.001mm；上下口分别横向检查 41 点
- 桥：两岸与16块桥板之间的17道接缝逐条检查，各41点，无超过1mm的高差/裂缝
- 梁：两梁对全部16块板进行800个承托点检查，接触误差 <0.001mm；四个梁端分别有0.250m真实体内嵌接
- 地基：六段铺装的下表面采样均有实际承托；高路及回落坡底层落在土岸体积内，低路为下达土体基准面的连续实心地基
- 岩土：两块岩的真实最低顶点均在各自土裙体内，距当地土表约0.306m；每岩450个下表面采样中342个嵌土，全部采样下方存在土体。其余是自然外露侧面，不将它们算作全底面埋土
- 静态路线：实际范围长34m，按1361个纵向站点 × 17个横向点采样，最大间距约25mm；每个高度共23137根净空射线，1.65m与1.8028m均无阻挡，无缺失地板列；最大相邻高差约0.125m；栏柱间最窄净宽约3.430m
- 净空横向宽度使用未改旧角色 T-pose 的真实1.663882m全宽。没有为新角色臆造胶囊或认定动态通行；1.65m仅为新身高目标
- 负控：内存中将 `BRIDGE_Deck08` 的真实位置抬高0.200m，重建世界网格并用同一检查，准确发现其前后两道坏缝；恢复真实变换后通过，未保存扰动文件

## Sockets 解释

- 台阶 IN 位于入口首级立面上的低路地坪基准，距首踏面0.125m，不是第一踏面；OUT 位于最高踏面出口
- 梁 IN/OUT 是梁底基准的有效跨度边界，间距4.000m；视觉全长4.500m，两端各延出0.250m供嵌岸，不能按视觉 BBox 直接当跨界
- 桥板、桥岸、土裙 sockets 均落在真实局部表面/边缘
- 岩 BASE 仅是最低高度加前沿原点的定位基准，**不是实体表面接触点**：其距最近岩面约0.358m。BASE 的 z 与岩底一致；实际入土必须使用真实岩底/土面，不能只对齐此点后声称接触

## 旧角色保持

独立对比 v2/v3：根以下9个对象的局部坐标、拓扑、UV、蒙皮权重、材料、修改器记录、骨架 rest 与本地变换一致。根旋转和缩放一致，仅平移改变。实测旧体型高度1.80279655m。

## 五张最终图实看

已逐张打开并检查 `CoreKit_Overview`、`Transition_Stair`、`Transition_Bridge`、`Transition_Rock`、`Transition_Side`。阶/岸/板/梁、土体承托及岩底嵌土的可见关系与几何检查一致；没有明显悬浮、断口或旧展示组污染。岩石已是修正后的源比例。接缝贴合导致桥板分块的视觉边线较弱，此项由实例/网格检查佐证。图像哈希与逐张观察记录见 JSON。

## 文件与重跑

- 主报告：`review/Independent_Geometry_Review.json`
- 五视图观察：`review/Independent_Visual_Review.json`
- 局部接口语义：`review/Independent_Local_Sockets.json`
- 新元数据模式：`review/Independent_Metadata_Review.json`
- 角色保持：`review/Independent_Character_Preservation.json`
- 独立脚本：`review/independent_review.py`、`review/review_local_sockets.py`、`review/fingerprint_baseline.py`

主检查：`blender -b deliverables/source/AetherLab_CoreKit_Transitions_v3.blend --python review/independent_review.py -- --output-report review/Independent_Geometry_Review.json`

输出路径可携带到任意恢复目录；源文件记录仅保留 basename。成功退出码0；检查失败写出报告后非零退出。

Sockets：`blender -b deliverables/source/AetherLab_CoreKit_Transitions_v3.blend --python review/review_local_sockets.py`

## 边界

这是静态候选模块/连通样段的几何验收。未进行 UE、导航、角色控制器、物理、结构承载、LOD、性能或完整世界关卡验收。UV 检查不宣称无重叠图集、统一 texel density 或最终美术批准。五张固定视图不能替代所有隐藏面的审查。
