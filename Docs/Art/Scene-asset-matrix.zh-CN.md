# AetherLab 全场景素材需求与生产矩阵

最新环境批次：已完成[南线环境层次v2](South-route-landscape-v2.zh-CN.md)。只精化出生点至城镇的环境层次，完整六区十二场景保留；不提升其余区域、25素材族或UE验收状态。

最新状态：已完成[GlobalBlockout_v1全图灰盒空间里程碑](Global-world-blockout-v1.zh-CN.md)。本文的v1/v2/v3小样记录保留历史范围；本次只有限放行全图布局/几何，不提升为25素材族、最终风格或UE验收完成。

最新最小过渡批次为SharedCore_v3，见本文末节；v2段落保留其历史范围与证据。
更新日期：2026-10-01。范围基线：main [`b7ab1f5`](https://github.com/fantuan812/AetherLab/commit/b7ab1f578d37548a4f2fd9f854117e0c91fcd628)。本表回答全游戏需要什么、现有素材在哪里、先优化什么、如何验收；不表示全部素材已制作。机器可读版本为 [Scene-asset-matrix.json](Scene-asset-matrix.json)，制作方法见 [场景生产规范](Scene-production-standards.zh-CN.md)。

## 一 范围和读表方法

范围为一个约 800×800 m 的连续世界，6 个地理区、12 个重点场景。下列范围和中心是 v4 的设计建议，不是本次已完成模型的实测范围。Lab、Field、Patrol / SupplyCache 是现存区内子点，不增加场景编号。

| 地理区 | 设计中心 m | 建议范围 | 场景 |
| --- | --- | --- | --- |
| 余炉镇 Hub | [0, 0] | 半径80～100米 | SCN_02、SCN_03、SCN_04、SCN_05、SCN_06、SCN_07 |
| 雨落山径 | [-65, -290] | 约80×180米 | SCN_01 |
| 灰烬林地 | [-270, 0] | 约140×180米 | SCN_08 |
| 旧水工坊 | [270, 0] | 约130×150米；60×80米多解核心位于其内 | SCN_09 |
| 断钟修道院 | [0, 270] | 约170×160米 | SCN_10、SCN_11 |
| 雷雨中继活动点 | [250, 220] | 约90×90米；塔高约12米 | SCN_12 |

审计范围为两个已核对 Blender 源文件（SCN01_TwoRoofs_Final、SCN01_Shelter_TA_Candidate）和基线提交下的 Art/AetherLab/Scenes。25 族中，8 族已有源待模块化、7 族部分源待补齐、10 族未检出专用生产模块。未检出仅限本次范围，先补查其他获准来源，再决定新建。

`asset_id` 是素材族的稳定 ID；`kit`、材质族和接口条目是本轮生产组织方案，尚不是全部已经存在的文件。逐个可导出 Mesh 在套件落地时另加清单。复用/改造/新增/远景表示生产途径；hero/通用/连接/装饰表示用途，允许同一族有多种用途。hero 是识别性锚点，不自动意味着高面数。

每场景用量先以设计明确要求为准，不把当前占位数量、远景房壳或任意散布数量当正式成品数量。资产族的使用场景同时核对已列 PROP 身份：如 SCN_01/03 的路标、SCN_03 的箱、SCN_06 的武器架已纳入相应族，JSON 标记这些由 PROP 合约补齐的关联。

## 二 十二场景需要的素材

| 场景与设计来源 | 主要素材需求 | 必须保留的空间或状态约束 | 既有 PROP |
| --- | --- | --- | --- |
| [SCN_01 雨落山径](https://github.com/fantuan812/AetherLab/blob/b7ab1f578d37548a4f2fd9f854117e0c91fcd628/Docs/Design-v4.zh-CN.md#L259-L271) | 湿石路/坡/浅沟、岩壁植被、石基木顶驿站、栏墙/灯链/路标；PROP_01水桶+独立水面、02物资箱、03水囊、04炉灯、06路标、20货车、21药袋布卷；小火/焦痕/湿痕 | 箱1、补给2、水桶1、小火1、路标2；货车轮轴/水桶/火源可分离；可见备用小径，不要求未学会的魔法 | PROP_01、PROP_02、PROP_03、PROP_04、PROP_06、PROP_20、PROP_21 |
| [SCN_02 余炉镇广场](https://github.com/fantuan812/AetherLab/blob/b7ab1f578d37548a4f2fd9f854117e0c91fcd628/Docs/Design-v4.zh-CN.md#L274-L286) | 石路/排水槽、石基木框抹灰两层房屋、灰绿/深蓝屋瓦、雨棚、窗门；PROP_05中央炉灯+井/饮水槽、07布告架、04路灯；长凳/花箱/货架 | 广场直径约35米；炉灯低矮；前后状态只改灯亮/供水，不换整城 | PROP_04、PROP_05、PROP_06、PROP_07 |
| [SCN_03 南门与入城小路](https://github.com/fantuan812/AetherLab/blob/b7ab1f578d37548a4f2fd9f854117e0c91fcd628/Docs/Design-v4.zh-CN.md#L289-L299) | 与山径共用道路/石墙/灯架/门构件；门洞、石墩、木门、带顶登记台、运货侧道；登记册/章/垫/收纳箱小物件 | 门洞约5米宽×6米高；登记台放门内侧；主路进城不可被可破坏门卡死 | PROP_02、PROP_04、PROP_06 |
| [SCN_04 武馆](https://github.com/fantuan812/AetherLab/blob/b7ab1f578d37548a4f2fd9f854117e0c91fcd628/Docs/Design-v4.zh-CN.md#L302-L314) | 城镇建筑套件室内版、木地板、边柱/梁/高窗/布幔、训练垫；PROP_08傀儡、09武器架；两训练位+导师示范区 | 约24×20米，净高至少5米；柱不进对练中心；受击件可拆、局部会话重置 | PROP_08、PROP_09 |
| [SCN_05 术院](https://github.com/fantuan812/AetherLab/blob/b7ab1f578d37548a4f2fd9f854117e0c91fcd628/Docs/Design-v4.zh-CN.md#L317-L329) | 小型温室式教学工坊；石台/木抽屉/低玻璃顶/铜架/绝缘脚；PROP_10材料试验台，木材/金属片/独立水槽/耐热托盘、水壶布巾 | 木/金属/水三试验位；干、点燃、浇湿状态可读；台间双人通道；电导部件与石绝缘分离 | PROP_10 |
| [SCN_06 工坊商店](https://github.com/fantuan812/AetherLab/blob/b7ab1f578d37548a4f2fd9f854117e0c91fcd628/Docs/Design-v4.zh-CN.md#L332-L344) | 共用半开放店铺壳、柜台、工作台、冷却水槽、浅盘/药瓶、木柜、工具卷/秤/小砧/夹具、材料短框；PROP_09武器架、21药袋布卷，生活照明 | 顾客站位和制作区分离；可交易陈列与背景工具分离；售罄用空格/收盘，不新建界面式场景物 | PROP_09、PROP_21 |
| [SCN_07 旅舍与队伍集结点](https://github.com/fantuan812/AetherLab/blob/b7ab1f578d37548a4f2fd9f854117e0c91fcd628/Docs/Design-v4.zh-CN.md#L347-L359) | 共用旅舍门廊/屋檐/圆角矮墙/院门/排水沟、长凳/毯子/饮水盆/花箱；PROP_07布告架、19休整灯座、04灯 | 小院约18×16米，四人站位、AI通道；NPC等待点分开；昼夜同布局 | PROP_04、PROP_07、PROP_19 |
| [SCN_08 灰烬林地](https://github.com/fantuan812/AetherLab/blob/b7ab1f578d37548a4f2fd9f854117e0c91fcd628/Docs/Design-v4.zh-CN.md#L362-L374) | 森林地表/湿草/河岸/浅水/岩块、树木与倒木、局部炭化落叶、货棚/木梁/可烧支撑；PROP_01水桶、15支撑、16绳；火/灭火残烟/焦痕 | 三指定火点+湿河岸+货棚；主路、绕行、捷径都可读；普通树与可破坏结构视觉不同 | PROP_01、PROP_15、PROP_16 |
| [SCN_09 旧水工坊](https://github.com/fantuan812/AetherLab/blob/b7ab1f578d37548a4f2fd9f854117e0c91fcd628/Docs/Design-v4.zh-CN.md#L377-L465) | 石渠槽/池底/步石/干平台、维护木栈道/桥/木板/栏杆、低塔/水轮；PROP_11机械水闸、12断路开关/移动导体变体、13线圈、15支撑、16绳、01有限水桶、02木箱；源/接收端/绝缘墩/独立水面 | 闸2、开关2、池1、石墩3；内部核心约60×80米，不另建地图；永久维护旁路、不要求鸣雷/凝霜；桥与绳、源与接点独立 | PROP_01、PROP_02、PROP_11、PROP_12、PROP_13、PROP_15、PROP_16 |
| [SCN_10 断钟修道院外部](https://github.com/fantuan812/AetherLab/blob/b7ab1f578d37548a4f2fd9f854117e0c91fcd628/Docs/Design-v4.zh-CN.md#L392-L404) | 石拱/窗/墙/柱/局部瓦模块、门框、缺损钟楼与铜钟、石木桥/真实挂点/维护道、浅渠、协作平台、近路开关；复用PROP_15/16及水/断电构件 | 主楼2～3层、钟楼略过树冠；并行入口和短安全整备区；不堵唯一退路、不扩成新副本群 | PROP_15、PROP_16 |
| [SCN_11 钟炉大厅](https://github.com/fantuan812/AetherLab/blob/b7ab1f578d37548a4f2fd9f854117e0c91fcd628/Docs/Design-v4.zh-CN.md#L407-L419) | 石地/嵌铜槽、圆形或宽八边大厅模块、外环/三主柱/供水平台/维修道、低中央炉台、断钟支架、百叶/炉窗/局部火线；复用水槽/闸阀/绝缘连接，PROP_17古印/18奖励箱若使用对应呈现 | 直径34～40米、连续外环至少4米、两供水点；只开放明确机关与局部支撑破坏；失败后可重入、击败可返回 | PROP_11、PROP_17、PROP_18 |
| [SCN_12 雷雨中继活动场地](https://github.com/fantuan812/AetherLab/blob/b7ab1f578d37548a4f2fd9f854117e0c91fcd628/Docs/Design-v4.zh-CN.md#L422-L434) | 石平台/可走外环/两条入侵小路/四低掩体；PROP_22中继塔核心、14接地杆×2、13铜环/陶绝缘族、04救援灯；真实地槽/导线/干石站位 | 约90×90米、塔高约12米；常态/失稳/稳定状态；电危险来自源与接触，雨湿不自动导电；结算停止战斗危险 | PROP_04、PROP_13、PROP_14、PROP_22 |

## 三 二十五素材族的来源与生产途径

状态仅描述来源成熟度。Collection 是找回源的定位证据，不证明这些源已经满足枢轴、UV、共享材质、LOD 或运行时要求。所有族当前均未完成族级生产验收；SharedCore_v2 已补核心道路/矮墙/围栏最小连接候选，独立22/22组及限定范围实看已通过。

| asset_id 与名称 | kit / 用途级别 | 使用场景 | 生产途径 / 来源状态 | 已定位源与下一步 |
| --- | --- | --- | --- | --- |
| `landscape` 基础地形与岩土 | Landscape / 通用、连接 | SCN_01、SCN_08、SCN_12 | 复用、改造、远景 / 已有源待模块化 | `REFINED_Valley`；已有连续谷地/岩石/山地；抽取地形与岩块族，远景与近景拆清 |
| `road_path` 通路与地面 | SharedCore / 通用、连接 | SCN_01、SCN_02、SCN_03、SCN_07、SCN_08、SCN_09、SCN_10、SCN_11、SCN_12 | 复用、改造、新增 / 已有源待模块化 | `SM_Path_4m`、`ENV_PathExtensions`、`SM_Wall_4m`、`SM_Fence_4m`、`SHR_Approved_EntryStep_Option`；道路、墙、栏杆、台阶均有源；规范枢轴、连接、净空和边缘而非重造 |
| `masonry` 石砌结构 | StoneStructure / 通用、连接 | SCN_01、SCN_02、SCN_03、SCN_04、SCN_05、SCN_06、SCN_07、SCN_09、SCN_10、SCN_11、SCN_12 | 复用、改造、新增 / 部分源待补齐 | `SM_Wall_4m`、`SCN01_Gate_Refinement`、`SCN01_House01_Refinement`；石块/石基/门框来源可复用；修道院石拱和室内专用组合未检出 |
| `timber_structure` 木构结构 | TimberStructure / 通用、连接 | SCN_01、SCN_02、SCN_03、SCN_04、SCN_05、SCN_06、SCN_07、SCN_08、SCN_09 | 复用、改造 / 已有源待模块化 | `SM_RestStop`、`SCN01_Shelter_Refinement`、`SCN01_House01_Refinement`；木柱梁墙门板已有源；按用途抽取，勿把整栋合成单块 |
| `roof_shelter` 屋顶雨棚 | RoofShelter / 通用、连接 | SCN_01、SCN_02、SCN_03、SCN_06、SCN_07、SCN_08、SCN_10 | 复用、改造、远景 / 已有源待模块化 | `SCN01_Shelter_Refinement`、`SCN01_House01_Refinement`、`SCN01_Two_Visible_Roof_Variants`、`REFINE_Staggered_Roofscape`；已有驿棚、屋瓦、两种可见屋顶；背景轮廓不视为可进入建筑 |
| `town_facade` 城镇建筑与室内套件 | TownArchitecture / 通用、连接 | SCN_02、SCN_03、SCN_04、SCN_05、SCN_06、SCN_07 | 复用、改造、新增、远景 / 部分源待补齐 | `SCN01_House01_Refinement`、`SCN01_Two_Visible_Roof_Variants`、`SET_DistantTownSilhouettes`；已有门窗房体；近景重复模块、室内和术院玻璃顶仍需补齐 |
| `vegetation` 植被与森林状态 | ValleyVegetation / 通用、装饰 | SCN_01、SCN_07、SCN_08 | 复用、改造、远景 / 已有源待模块化 | `ENV_Foliage`、`REFINED_Valley`、`REFINE_Connected_Valley_Greenbelts`；树、草、叶片与河谷植被已有源；有效复用/LOD/透明材质仍需审查 |
| `lighting` 生活照明与路标焦点 | EverydayLights / 通用、装饰 | SCN_01、SCN_02、SCN_03、SCN_06、SCN_07、SCN_10、SCN_12 | 复用、改造 / 已有源待模块化 | `SM_LanternPost`、`SCN01_Shelter_Refinement`、`SM_SouthGate`；路灯与生活灯已有；灯体、发光材质、运行灯光独立 |
| `container_supply` 容器与补给 | SupplyProps / 通用 | SCN_01、SCN_03、SCN_06、SCN_08、SCN_09 | 复用、改造、新增 / 部分源待补齐 | `SM_WaterBarrel`、`SM_BrokenCart`；桶、独立水面、货箱箱盖已有；旅行水囊/药袋/布卷等未逐项定位 |
| `cart` 货车族 | CargoCart / 通用 | SCN_01 | 复用、改造 / 已有源待模块化 | `SM_BrokenCart`；车身、轮轴、布篷、货箱已有；保留零件边界 |
| `water_surface` 水槽水面及冰代理 | WaterChannels / 通用、连接 | SCN_01、SCN_02、SCN_05、SCN_06、SCN_08、SCN_09、SCN_10、SCN_11 | 复用、改造、新增 / 已有源待模块化 | `SM_WaterBarrel`、`REFINED_Valley`；桶水/雨水/溪流为独立源；正式可冻结/导电代理须按玩法另外验证 |
| `reactive_fire` 燃料及火湿状态 | ReactiveSurfaces / 通用、装饰 | SCN_01、SCN_05、SCN_08、SCN_11 | 复用、改造、新增 / 部分源待补齐 | `FX_ShelteredFire`、`SM_BrokenCart`；火焰/干草/焦草源存在；可复用燃料与状态绑定仍需加工 |
| `furniture_signage` 生活陈设与任务牌 | TownFurnishings / 通用、装饰 | SCN_01、SCN_02、SCN_03、SCN_04、SCN_06、SCN_07 | 复用、改造、新增 / 部分源待补齐 | `SCN01_Shelter_Refinement`、`SM_RestStop`；长凳/工作台等已有；专用路标/布告架/花箱与纸页模块未逐项确认 |
| `registration_station` 登记站 | TownServices / hero、通用 | SCN_03 | 新增、复用 / 专用生产模块未检出 | 本范围专用源未检出；木桌棚可借已有源；登记桌成套与册簿印章未检出 |
| `training_equipment` 教学训练设施 | TrainingProps / 通用 | SCN_04、SCN_05、SCN_06 | 新增、复用 / 专用生产模块未检出 | 本范围专用源未检出；武馆训练傀儡/武器架与术院材料台专用美术模块未检出；运行灰盒存在不等于美术完成 |
| `shop_workbench` 商店与制作陈设 | TownServices / 通用、装饰 | SCN_06 | 复用、改造、新增 / 部分源待补齐 | `SCN01_Shelter_Refinement`；工作台/长凳木件可复用；交易柜台/砧具/药瓶陈列专用套件待补 |
| `hub_hearth` Hub炉灯与水井 | HubLandmarks / hero | SCN_02 | 新增、复用 / 专用生产模块未检出 | 本范围专用源未检出；中央炉灯、水井及修复状态专用模块未检出 |
| `rest_checkpoint` 休整与集结 | TownServices / hero、通用 | SCN_07 | 新增、复用 / 专用生产模块未检出 | 本范围专用源未检出；休整灯座未检出；庭院建筑壳/灯/长凳来自共有族 |
| `bridge_support` 桥绳与预置结构 | BridgeStructure / 连接、通用 | SCN_08、SCN_09、SCN_10 | 复用、改造、新增 / 部分源待补齐 | `SM_BrokenCart`、`SCN01_Shelter_Refinement`；木板、绳、梁有源；可落桥总成、明确可割锚点、断裂状态和机械约束合规模块待制作 |
| `waterworks_mechanics` 水利机械 | Waterworks / hero、通用 | SCN_09、SCN_11 | 新增、复用 / 专用生产模块未检出 | 本范围专用源未检出；渠闸、手轮、水轮、调压塔专用源未检出 |
| `electrical_components` 古代导能与绝缘 | AncientConduction / 通用、连接 | SCN_05、SCN_09、SCN_10、SCN_11、SCN_12 | 新增、复用 / 专用生产模块未检出 | 本范围专用源未检出；导能源/接收端/导体开关/线圈/陶绝缘成套生产模块未检出 |
| `abbey_architecture` 修道院锚点 | AbbeyLandmarks / hero、连接 | SCN_10 | 新增、复用 / 专用生产模块未检出 | 本范围专用源未检出；修道院钟楼/铜钟/侧廊专用源未检出；基础石木瓦可复用 |
| `bell_furnace` 钟炉大厅锚点 | BellFurnace / hero | SCN_11 | 新增、复用 / 专用生产模块未检出 | 本范围专用源未检出；钟炉、断钟支架、炉窗百叶专用源未检出 |
| `reward_objects` 结算视觉物 | RewardProps / hero、通用 | SCN_11 | 新增、复用 / 专用生产模块未检出 | 本范围专用源未检出；古印/奖励箱专用呈现未检出；货箱源可供合规变体 |
| `relay_core` 中继设施锚点 | RelayLandmark / hero | SCN_12 | 新增、复用 / 专用生产模块未检出 | 本范围专用源未检出；中继核心/接地杆专用模块未检出；平台与掩体可用共有基件 |

## 四 尺寸接口 材质和验收证据

下列接口是待落实的生产契约。只有明确标为设计要求的尺寸可作为既有依据；4 m 是这批 Path/Wall/Fence 的候选节距，坡道升高 1 m 是试拼变体，均不是全项目统一通行标准。材质短名表示共享族，不表示新材质已经做完。

| asset_id | 尺寸与接口 | 共享材质族 | 必须补齐的验收证据 |
| --- | --- | --- | --- |
| `landscape` | 近景地形/河岸与远景分件；按道路标高接合，岩地过渡待试拼 | earth、stone、foliage | 近远分层、坡侧/河岸/岩地接合图与人物尺度图 |
| `road_path` | 本批Path/Wall/Fence候选节距4m；净宽和视觉BBox另记；v2补最小转角/交叉/端头/路地和4m升1m跟坡试拼，独立22/22组及限定范围实看已通过；台阶等缺口保留 | stone、earth、timber | 实际网格覆盖/接缝/标高、唯一共享柱、几何净空和双角色尺度；v2仅覆盖最小连接试拼 |
| `masonry` | 基础/墙/拱/门框分件；与门洞、地坪、屋檐连接；拱跨不预设统一值 | stone | 内外角、端头、门洞、拱脚、地坪接合及通行净空 |
| `timber_structure` | 梁柱墙板分件；锚点对应真实支承；普通结构与玩法受击件分开 | timber、metal | 梁柱/墙顶交界、非均匀缩放清理和运动/受击边界 |
| `roof_shelter` | 屋面/檐口/屋脊/端部/雨棚独立；两可见屋顶类型优先回收 | slate、timber | 内外角、檐口/墙头、背面、屋脊和近远轮廓 |
| `town_facade` | 门窗/抹灰填充/店面/门廊与室内地坪配对；温室顶待补 | stone、timber、plaster、glass | 同套件至少完成服务建筑灰盒组合；远景壳不能当室内 |
| `vegetation` | 树/草/灌木/倒木分层；落地锚点；普通植物与可破坏支撑区别 | foliage、timber | 自建图集来源、卡片/透明边缘、遮挡路线和近中远轮廓 |
| `lighting` | 灯架挂点、灯体、发光和运行灯光职责分开；4m不限制灯间距 | metal、glass、timber | 源材质/贴图可回读、灯体与光效边界、路口可读性 |
| `container_supply` | 桶与水面、箱与盖分离；水囊/药袋/布卷按道具尺度补查 | timber、metal、cloth、water | 干/满/倒地/开盖与携带状态；未定位小物逐项补查 |
| `cart` | 车身/轮轴/布篷/货箱/绳绑保留零件界限；仅指定连接可损坏 | timber、metal、cloth | 实际轮轴/锚点、指定损坏件和完整/事故状态图 |
| `water_surface` | 水面独立于容器/池底；水位/流向/指定冰通路代理分开 | water、stone | 桶水/浅渠/水池接合；冻结/导电为待验证玩法代理 |
| `reactive_fire` | 燃料与火/湿痕/炭化/残烟分层；状态不换掉基础物件身份 | timber、earth、fx | 干/湿/燃烧/熄灭对照；渲染效果与规则代理分别记录 |
| `furniture_signage` | 长凳/花箱/架体共用；路标地名与布告纸页可替换 | timber、metal、cloth | 文字面/纸页替换、站位和通道；专用架/标来源补查 |
| `registration_station` | 门内带顶桌台；复用木桌棚；册簿/印章/布垫可分件 | timber、cloth、metal | 登记交互站位、侧道与入城主路；成套专用源待补 |
| `training_equipment` | 傀儡受击块/武器架/三材料台分件；武馆净高≥5m；台间双人 | timber、metal、stone、water | 受击/重置边界、剑盾锤尺寸、材料分区和站位 |
| `shop_workbench` | 柜台/工作台/冷却槽/陈列分开；顾客与制作位分离 | timber、metal、ceramic、glass | 可交易陈列与背景工具分离、售罄变体、交互净空 |
| `hub_hearth` | 广场约35m；低炉灯/井/饮水槽独立；结构保留，灯亮和供水变状态 | stone、metal、water、glass | 修复前后对照、广场视线和路口净空 |
| `rest_checkpoint` | 院子约18×16m；低灯座、四人站位、NPC等待点与通道分开 | stone、metal、glass | 四人/NPC尺度占位、昼夜同布局、返程落点 |
| `bridge_support` | 桥板/梁/绳/支撑/两个真实锚点；桥岸标高和永久维护旁路 | timber、metal、stone | 张紧/松落、破坏/落桥状态、桥岸接合；不能仅复用绳外观 |
| `waterworks_mechanics` | 闸轨/手轮轴/拉杆/水轮/流路分离；水工坊2闸2开关1池3墩按设计 | stone、metal、timber、water | 运动轴/开闭限位、流路、维护旁路和机械状态图 |
| `electrical_components` | 固定源/接收端/开关/导体/线圈/绝缘脚分件；真实接点与地槽 | metal、ceramic、stone | 接触/断开、操作端/危险端、导线连续；雨湿与通电独立 |
| `abbey_architecture` | 石木瓦基件复用；主楼2～3层、钟楼略过树冠；前庭/水道/侧廊连接 | stone、metal、slate | 地标轮廓、并行入口、整备区、返回近路 |
| `bell_furnace` | 大厅直径34～40m；连续外环≥4m；低炉台/断钟支架/百叶/两供水点 | stone、metal、water、fx | 外环净空、局部破坏/炉窗状态、失败重入与击败后返回空间 |
| `reward_objects` | 奖励箱可借货箱源派生；古印正背对应；领取/空箱身份一致 | timber、metal、stone | 正背与开启状态对照；不新增奖励系统或未定展示位置 |
| `relay_core` | 场地约90×90m、塔约12m；两接地杆、四低掩体、两入侵路和外环 | stone、metal、ceramic、glass | 常态/失稳/稳定、源接点/干石站位、结算危险停止的美术状态 |

材质族说明：

- `stone`：湿石/玄武岩族，优先Wet basalt与已有石基材质
- `earth`：湿泥/山地土族，优先Wet mountain earth与已有地形材质
- `timber`：旧木/雪松族，优先Aged cedar、SHR与H01木材源
- `slate`：灰绿/深蓝瓦族，优先SHR/H01瓦源，避免每瓦复制材质
- `plaster`：抹灰族，优先H01_Lime_plaster等已有源
- `foliage`：自建叶草图集与Rain moss，核对AtlasUV/透明边缘
- `metal`：旧铁/铜族，优先Forged iron与Lantern aged brass；导体身份另记
- `glass`：灯罩/窗/温室玻璃族；现有灯窗源与待补变体分开
- `cloth`：亚麻/布篷/补给布族，优先已有棚屋/货车源
- `water`：独立水面族，优先Rainwater等源；冰/导电代理待补
- `ceramic`：陶/绝缘族；可回收SHR_Matte_stoneware，专用绝缘模块待补
- `fx`：火/烟/湿痕状态表现族，优先FX_ShelteredFire；不代表规则完成

### 本次审计与候选文件

当前已完成限定范围验收的连接候选见 [SharedCore_v2 说明](../../Art/AetherLab/Kits/SharedCore_v2/README.zh-CN.md)。以下 v1 审计和验证保留为历史来源证据，不能代替 v2 的独立重开验收。

- [来源对照与范围](../../Art/AetherLab/Kits/SharedCore_v1/docs/audit/region_scope.json)：场景、PROP、设计来源和25族定位记录
- [Collection 汇总](../../Art/AetherLab/Kits/SharedCore_v1/docs/audit/asset_collections_summary.json)：源集合、基础网格计数、材质和UV字段
- [显示层与材质节点审计](../../Art/AetherLab/Kits/SharedCore_v1/docs/audit/scene_runtime_audit.json)：来源场景的可见性和材质节点读取；不代表运行时性能
- [角色尺度审计](../../Art/AetherLab/Kits/SharedCore_v1/docs/audit/character_scale_audit.json)：65骨baseline的实际对象/网格边界
- [SharedCore 独立验证](../../Art/AetherLab/Kits/SharedCore_v1/docs/CoreKit_Validation.json)、[Blender 源（Library交付说明）](../../Art/AetherLab/Kits/SharedCore_v1/README.zh-CN.md)、[总览（Library交付说明）](../../Art/AetherLab/Kits/SharedCore_v1/README.zh-CN.md)、[试拼近照（Library交付说明）](../../Art/AetherLab/Kits/SharedCore_v1/README.zh-CN.md)

Source Collection 审计、显示层计数与候选验证是不同证据：隐藏历史、冻结高模、多个 LOD 不得全部算作当前渲染量；基础三角数不能换算 GPU 性能。当前不要求 UE 验收。

## 五 既有公共道具身份与状态

沿用 v4 的 PROP_01～PROP_22，不给同一种桶、灯、箱按场景另起身份。专用组合可以由多个共用模块组成。下表场景是本次明确列出的设计引用，不限制后续在既有区内进行获准复用；并不声明所有道具已有合格模型。

| 道具 | 归属素材族 | 明确使用场景 | 状态或拆分契约 |
| --- | --- | --- | --- |
| PROP_01 水桶 | `container_supply` | SCN_01、SCN_08、SCN_09 | 独立水面；干/满水/倒地 |
| PROP_02 物资箱 | `container_supply` | SCN_01、SCN_03、SCN_09 | 箱盖独立 |
| PROP_03 旅行水囊 | `container_supply` | SCN_01 | 满/空 |
| PROP_04 手提炉灯 | `lighting` | SCN_01、SCN_02、SCN_03、SCN_07、SCN_12 | 灯体与光效分离 |
| PROP_05 中央炉灯 | `hub_hearth` | SCN_02 | 修复前/后 |
| PROP_06 木路标 | `furniture_signage` | SCN_01、SCN_02、SCN_03 | 地名后续贴花 |
| PROP_07 布告架 | `furniture_signage` | SCN_02、SCN_07 | 可替换纸页 |
| PROP_08 训练傀儡 | `training_equipment` | SCN_04 | 受击块可拆 |
| PROP_09 武器架 | `training_equipment` | SCN_04、SCN_06 | 剑盾锤尺寸真实 |
| PROP_10 材料试验台 | `training_equipment` | SCN_05 | 木/金属/水分区 |
| PROP_11 机械水闸 | `waterworks_mechanics` | SCN_09、SCN_11 | 轨道/轴一致 |
| PROP_12 导体断路开关 | `electrical_components` | SCN_09 | 接触/断开；可移动桥片变体 |
| PROP_13 线圈导体 | `electrical_components` | SCN_09、SCN_12 | 实际连接路径 |
| PROP_14 接地杆 | `electrical_components` | SCN_12 | 操作端与危险端区分 |
| PROP_15 脆弱木支撑 | `bridge_support` | SCN_08、SCN_09、SCN_10 | 受力、裂纹、连接明确 |
| PROP_16 可割绳索 | `bridge_support` | SCN_08、SCN_09、SCN_10 | 两真实锚点；张紧/松落 |
| PROP_17 古印 | `reward_objects` | SCN_11 | 正背一致 |
| PROP_18 奖励箱 | `reward_objects` | SCN_11 | 可领取/空箱 |
| PROP_19 休整灯座 | `rest_checkpoint` | SCN_07 | 低且不挡路 |
| PROP_20 货车 | `cart` | SCN_01 | 指定损坏连接件 |
| PROP_21 药袋与布卷 | `container_supply` | SCN_01、SCN_06 | 生活补给 |
| PROP_22 中继塔核心 | `relay_core` | SCN_12 | 闭合/失稳/稳定 |

来源：[Design v4 公共道具与状态](https://github.com/fantuan812/AetherLab/blob/b7ab1f578d37548a4f2fd9f854117e0c91fcd628/Docs/Design-v4.zh-CN.md#L2584-L2613)。SCN_11 的古印/奖励箱按设计中实际采用的对应呈现处理，不借素材表新增结算系统。

## 六 连续地图连接与既有子点

| 连接 | 性质 | 布局必须证明 |
| --- | --- | --- |
| SCN_01 → SCN_03 | 主线步行 | 野外出生到南门连续可走，救援可绕 |
| SCN_03 → SCN_02 | 主线步行 | 门后可见广场炉灯，登记台不挡行走 |
| SCN_02 → SCN_04 | Hub短步行往返 | 服务容易辨认，AI与第三人称镜头可通行 |
| SCN_02 → SCN_05 | Hub短步行往返 | 服务容易辨认，AI与第三人称镜头可通行 |
| SCN_02 → SCN_06 | Hub短步行往返 | 服务容易辨认，AI与第三人称镜头可通行 |
| SCN_02 → SCN_07 | Hub短步行往返 | 服务容易辨认，AI与第三人称镜头可通行 |
| town → SCN_08 | 西侧外勤 | 西门主路与回环联系 |
| town → SCN_09 | 东侧外勤 | 东门主路与维护旁路 |
| town → SCN_10 | 北侧挑战 | 组队入口与可返程路 |
| SCN_10 → SCN_11 | 同一区内挑战 | 前庭→水道→侧廊→钟炉，保留整备点与退路 |
| SCN_09 → SCN_12 | 东北环路东段 | 按当前已定义环路联系，不另增区域 |
| SCN_12 → SCN_10 | 东北环路北段 | 按当前已定义环路联系，不另增区域 |

其中 town 指余炉镇整体。任务顺序保持 Q01→Q02→Q03→Q04/Q05→Q06→Q07→Q08；Q04/Q05 可交换，但 Q06 要求二者都完成。地图存在合法旁路不改变个人资格。

| 既有子点 | 归属说明 | 复用需求 |
| --- | --- | --- |
| Lab | 余炉镇范围内的现存反应教学/复用子点 | 火/水桶/井/箱/台阶/旁路/绳桥/水面/电源/接收端/导体/手动泵 |
| Field | 当前Frontier连续世界西北连接带的既有反应子点；不擅自声明为独立主题区 | 火/桶/箱/源/接收端/导体/绳桥/地板 |
| Patrol/SupplyCache | 林地、水工坊、修道院的日常子点 | 巡逻锚点/补给箱 |

以上子点依照 [WorldObjects.json](https://github.com/fantuan812/AetherLab/blob/b7ab1f578d37548a4f2fd9f854117e0c91fcd628/Content/AetherCore/Definitions/WorldObjects.json) 与 [V10 场景旅程](../Planning/V10-scene-and-journey.zh-CN.md)核对；运行占位不等于独立美术空间已完成。

## 七 本轮核心道路矮墙围栏连接候选

SharedCore_v2 使用独立源模型，保留 v1 的9个母模块，新增13个最小连接/支撑/跟坡派生件，共22个母件；复用既有路石、墙、栏、共享墩柱、灯架网格及石/土/木材质。作者保存后重开报告记录22个母件、678处实例共用21份网格；27/27项检查通过，独立22/22组及限定范围实看已通过。未使用的灯母件另有112个继承退化四边形（224个三角形）及40个零面积UV多边形，不能据此写全部22网格质量通过。未扩建全图，未开始单栋精雕，也不表示25族完成。

原9件的稳定 asset_id、来源对象和 v1 历史证据保留在 JSON；v2 对这些母件的保留不能直接沿用 v1 的通过数。道路转角/T/X主要由既有4m路面组合及可旋转的1m路肩件完成，端盖复用原墩柱，不为每种试拼另造完整网格。

| 保留 asset_id | 本轮用途 |
| --- | --- |
| `KIT_Path_Flagstone_4x4_A` | 平道路石与高端平台 |
| `KIT_Path_Substrate_4x4` | 原薄路床；高端平台继续复用 |
| `KIT_WallLow_Run_4m` | 矮墙直段 |
| `KIT_WallLow_EndCornerPier` | 唯一共享墩、混合连接墩与墙端盖 |
| `KIT_Fence_Rails_4m` | 围栏直段与高端转接 |
| `KIT_Fence_SharedPost` | 围栏接柱、跨中柱与外端柱 |
| `KIT_LanternPost` | 保留灯架母件；不新增照明验收 |
| `KIT_Path_Ramp_4m_Rise1m` | 4m升1m测试坡路 |
| `KIT_Substrate_Ramp_4m_Rise1m` | 坡路薄路床 |

| 新增 asset_id | 生产途径 | 最小连接职责 |
| --- | --- | --- |
| `KIT_Path_Foundation_4x4` | 改造 | 4×4m道路底座；由原路床加深至局部Z=-0.55m，闭合道路支撑 |
| `KIT_Wall_Footing_4m` | 新增连接件，复用材质 | 4m矮墙基脚；名义横宽0.68m，局部Z=-0.155～0.10m |
| `KIT_WallLow_Rise_4m_1m` | 改造 | 原矮墙沿局部Y每4m升1m；固定派生网格，不逐实例拉伸 |
| `KIT_Wall_Footing_Rise_4m_1m` | 改造 | 4m墙基脚随坡升1m，与坡墙配对 |
| `KIT_Fence_Rails_Rise_4m_1m` | 改造 | 原围栏横梁沿局部Y每4m升1m；柱保持直立 |
| `KIT_Shoulder_Straight_1m` | 新增连接件，复用材质 | 1×1m路肩；道路侧局部Z=-0.035m接地侧-0.35m，底-0.55m |
| `KIT_Shoulder_Convex90_1m` | 新增连接件，复用材质 | 1×1m外凸90度路肩闭口；配直路肩覆盖道路外角 |
| `KIT_Shoulder_Concave90_1m` | 新增连接件，复用材质 | 1×1m内凹90度路肩闭口；配直路肩覆盖道路内角及T/X边界 |
| `KIT_Terrain_Patch_1m` | 新增连接件，复用材质 | 1×1m土块；局部Z=-0.55～-0.35m，用于端头和试拼支撑 |
| `KIT_Terrain_RaisedPatch_1m` | 新增连接件，复用材质 | 1×1m实心高台土基；顶Z=0.965m、底Z=-0.55m，补高端转接底部悬边 |
| `KIT_Ramp_Bank_4m_Rise1m` | 新增连接件，复用材质 | 4m长升1m坡路实体土基；两侧坡脚回接平地，中央承托坡路床 |
| `KIT_Raised_Landing_Bank_4m_Height1m` | 新增连接件，复用材质 | 4m长高位平台实体土基；承托+1m路面，两侧坡脚回接平地 |
| `KIT_Raised_EndBank_1m` | 改造 | 高台端岸1m；由高位土基派生，中央槽填平至Z=0.965m |

[22母件接口清单](../../Art/AetherLab/Kits/SharedCore_v2/docs/Module_Interfaces.json) 记录局部接口、视觉包围盒、原点、前向/上向、旋转/缩放约束与材质。接口坐标须乘实例矩阵，不把名义路面标高当网格边界。

最后修正母件及实例继承的坡件 `OUT.z=0` 元数据，统一22母件局部接口、对应Empty和678实例锚点属性；跟坡件OUT为 `(0,4,1)`。最终源SHA-256为 `9da7bdfc2731c144cb31d0866b9574a24d9f9057e6ed38895226d6371e3ac62b`。刷新前后完整网格/UV/材质引用/对象矩阵/相机指纹均为 `1e620efc234bc3a3c0a2744d7ec31ed7edc89c3829b4d1690fd13079d0f494a0`；独立试拼几何指纹前后均为 `ee2ae912ca6796d2cc9dd26711b0a8d149ce9706fd249c777ee62d11d198c457`。最终源作者27组与独立22组均已重跑通过。

以上尺寸是候选构建参数，不能当作独立实测结果。4m仍是本批节距候选；1m路肩片是局部连接子件，不确立全局网格。升1m/4m坡道不升级为全项目坡度标准。试拼保持1.65m设计标尺与约1.8028m的原65骨角色双参照，角色不缩放。

### 七组试拼和本轮验收状态

| 试拼 ID | 范围 | 独立验收必须核对 |
| --- | --- | --- |
| `ROAD_LOOP_T` | 真实闭合道路环及T支路 | 路面/路基覆盖、内外90度路肩、闭环与T口连续性 |
| `ROAD_X` | 独立道路X交叉 | 四向道路与交叉内凹角路肩的连续覆盖 |
| `ROAD_EARTH_END` | 道路端头接土 | X交叉南臂末端经路肩接土块；不扩大为完整路地系列 |
| `WALL_LOOP` | 矮墙闭环 | 直段、90度角、连续基脚与每接点唯一共享墩 |
| `FENCE_LOOP` | 围栏闭环 | 直段、90度角、每接点唯一共享柱和独立跨中柱 |
| `MIXED_X_ENDS` | 矮墙与围栏混合X及端盖 | 两墙两栏共用一个中心墩；外端墩/柱收口 |
| `HEIGHT_MIX` | 4m升1m坡路与墙栏跟坡 | 低路→坡路→高平台、实体土基、坡墙/坡基脚/坡栏、直立柱与高端墙栏转接 |

作者重开检查27/27组与独立检查22/22组分别通过，后者为21组几何/数据/负控加1组接口元数据一致性；不能合写成49项统一测试。独立脚本未调用作者验证器，最终源SHA-256为 `9da7bdfc2731c144cb31d0866b9574a24d9f9057e6ed38895226d6371e3ac62b`。几何检查覆盖道路环/T/X整面采样与接缝、路土端头、坡岸、44柱墩底截面承托、54处墙栏端部真实嵌接，以及21个实际使用母网格的闭合/正体积/退化/UV非零面积投影；22母件、对应Empty与678实例接口属性一致。

坡路中央净宽63个采样截面最小约2.67485m；1.65m与1.8028m两高度、直径0.8m的静态几何包络检查通过，保守侧向余量约0.48039m。该结论仅限记录路径与采样，不能提升为全项目净宽或UE碰撞/导航标准。将端栏柱临时抬高0.60m的负控成功触发4处预期承托失败，恢复内存变换且未保存原文件。9个角色对象另与原65骨baseline比对，几何/拓扑/权重/组名/骨架rest保持检查通过。

五张作者预览（总览、道路顶视、坡路近景、高差侧视、混合交叉）及独立X顶视均已打开实看，未发现本批连接的明显裂口或悬空。作者图像来自接口元数据刷新前SHA `f3644928a7e85e93fee67f8157a17b2a044c31f102bc1fc4173ba103c8a0870d`，刷新后未声称另渲染；前后独立试拼几何指纹和完整网格/UV/材质引用/矩阵/相机指纹一致，故可作最终几何证据。独立X顶视为Workbench几何审查，不作材质风格结论。

初次独立审查发现底面悬边、旧路床反法线和竖面UV塌线，v2生产副本已修复，原v1文件未改。构建清单的 `awaiting_reopen` 只是构建时状态，最终几何结论以两份重开报告及角色保存报告为准。

便携重建已验证：从未修改的v1输入（SHA-256 `451da979d79475cbed1ae9974dfe7193458d6d6899f9269434a125480fc18b25`）在新输出目录重建，保存后重开27/27组通过；网格、UV、材质引用、对象矩阵与相机指纹和最终交付一致（`1e620efc234bc3a3c0a2744d7ec31ed7edc89c3829b4d1690fd13079d0f494a0`）。指纹读取前显式刷新依赖图，避免构建期尚未更新的矩阵造成报告差异。

已补的是最小核心连接试拼，仍未覆盖完整道路边缘/端头和不同净宽系列、台阶、桥岸/岩地/完整路地过渡、其余建筑与玩法套件。全套件门尚未整体放行，不能据此批量铺全图。材质/纹理最终原画对照、LOD、lightmap与运行时表现未验。

二进制、图片和恢复包仅通过Library交付，并沿用原文件身份更新版本；版本、字节数与哈希见 `Art/AetherLab/Kits/SharedCore_v2/docs/Delivery_Manifest.json`。Git只保留中文说明、矩阵、构建/检查脚本和文本报告。详见 [v2交付与验收边界](../../Art/AetherLab/Kits/SharedCore_v2/README.zh-CN.md)。

## 八 生产队列和需要决定的事项

1. 需求确认与来源补查：保留6区12场景和公共道具身份；为10族专用缺口补查可用源，找到后更新来源状态，不重复造轮子
2. 共用源优化：先处理地形/通路、石木构、屋顶、植被、灯和容器；保留自建材质与贴图，建立母件、共享接口和少量变体
3. 套件闭环：在已验收的v2最小核心连接基础上补未覆盖的台阶、完整边缘/端头及桥岸/岩地等异类过渡；对水利、桥、电导、教学和各地标建立必要功能灰盒，先证明形体与状态组合可用
4. 全局建模：套件通过后再做连续地形、6区体块、12场景与全部连接；先放共用件与功能占位，不先雕满一条街
5. 局部细化：在稳定全图上按主要镜头补城镇服务、林地状态、水工坊、修道院/钟炉、中继塔的独特锚点；装饰最后进入，并回收可复用零件

需明确的事项：CHR_01设计165 cm与现有65骨网格约180.28 cm的差异如何处理；每类空间的正式净空与镜头条件；本批4 m接口是否在试拼后采用、适用于哪些套件；专用缺口的来源与制作归属。当前不擅自修改角色或把候选网格、面数、纹素密度扩成全项目标准。

## 九 矩阵维护规则

新增或合格化一个模块时，更新该族的来源、接口、生产状态和证据；不要只把“已有源”改写成“完成”。新增需求先映射既有族与PROP/SCN身份，再说明为什么需要独特模块。中文矩阵与JSON同时改；验收日志和脚本留在Git套件目录，资产本体/图片/恢复包只存Library，并用交付清单记录同一文件身份的新版本。此表不替代 [Design v4](../Design-v4.zh-CN.md) 或重写旧正式设计数值。


## 九 最小地形过渡批次 v3

保留v2全部22母件，新补6件：实心8级石阶、桥面板、桥梁、桥岸台、岩地土裙、原比例河岩。77个关联实例形成34m组合样段：低路→台阶→高路→岸桥岸→高路→返坡→低路。复用石/土/木三族已有材质，没有新增材质datablock。

新增件只补全图基础铺设需要的最小接口。石阶高差1m/进深4m、桥有效跨4m/宽4m、土裙长宽2m均为本批候选；不扩为全球格网或通用尺寸。维护桥板/梁/岸台分件，不代表可落桥、绳锚点和破坏状态完成。源及实测证据见[SharedCore_v3](../../Art/AetherLab/Kits/SharedCore_v3/README.zh-CN.md)。

[六区十二场景覆盖清单](Global-blockout-coverage-v3.zh-CN.md)区分可复用基础、必须占位的锚点、需在全图证明的连接和四层放行条件。对应最小连接通过后，可以开始基础地形、路线、六区体块和十二场景功能粗模；无需先精雕所有建筑，也不无限预造变体。25族来源成熟度、其余建筑/机关/状态缺口保留，全套件门、全图门均未整体通过。

当前新女主目标1.65m仅用标尺核对；约1.8028m旧65骨baseline保留原几何、骨架与权重，不能称它为新女主。Windward及银发角色未编辑。本批不安装运行UE，不作导航、碰撞、运行性能声明。

最终v3源SHA-256为`46fc07828d7b3a19d7362f41e0cf03da79df321ea92172fa421f8b6b841767b3`。6新增母件闭合/正体积/无退化和SurfaceUV面积检查通过；31个新母件及其实例的接口元数据无继承冲突；77实例关联复用。真实8级台阶、17道桥板/岸接缝、两梁四端各0.25m入岸、岩底入土与34m样段静态采样通过。实际抬高Deck08的0.2m负控触发两道坏缝，恢复通过；5张最终PNG均独立实看。净空只覆盖记录的1.66388m横向采样宽度及1.65/1.8028m两高度，不作动态角色或全项目标准。
