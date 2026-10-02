# SharedCore v3 最小地形连接批次

本批承接v2。保留22个旧母件，新增6个候选；77个关联实例组成一条34m组合样段。它不是六区全图，也不是完整桥、楼梯或地形素材族。UE代码未编译、未测试；执行记录中的Blender制作/几何审查和Python脚本语法检查。不安装或运行UE。

## 最小新增清单

| 母件 | 来源与必要改动 | 本批候选接口 |
|---|---|---|
| KIT_Stair_Solid_4m_Rise1m | 已有H01门前石阶的构造参照；新建连续闭合阶梯轮廓，复用湿玄武岩材质 | 宽4m、8×0.125m高差、8×0.5m进深；IN为低路支承地坪，不是首踏面 |
| KIT_Bridge_DeckPlank_4x0p25 | SHR_Workbench_top_plank八顶点网格派生，沿用共享旧木材质 | 每板4×0.25m；16个关联实例组成4m桥面 |
| KIT_Bridge_Bearer_4p5m | SHR_Tie_beam八顶点网格派生，沿用共享旧木材质 | 总长4.5m、有效跨界4m，两端各入岸0.25m |
| KIT_Bridge_Abutment_6x1m | H01_Door_stone_step网格派生、共享湿玄武岩材质 | 宽6m、深1m、顶面与高路底床连续 |
| KIT_RockToe_Earth_2m | v2直路肩拓扑派生，复用同一湿土材质 | 路侧0m到地侧2m，长2m；实体底部加深以埋岩 |
| KIT_Rock_River_A | REFINED_Valley/River rock原42顶点拓扑与实际世界形体，烘焙已有变换后平移 | 实际尺寸约0.7605×1.4966×0.9460m；本批埋底基准-0.5m，实例不缩放；BASE只是最低高度/前原点基准，不是实体接触点 |

4m只限这批接口候选；不得扩为全项目格网或道路净宽标准。台阶不是所有室内楼梯或无障碍坡度标准，固定桥不是落桥机关或结构承载验证。新女主目标1.65m用标尺表达；旧65骨baseline约1.8028m只作未缩放的历史几何参照。

## 组合样段与来源保护

低路→台阶→高路→近岸→桥板/双梁→远岸→高路→返坡→低路；一段路侧通过两个相同土裙与两块原比例岩体收边。其他路、坡、栏、立柱直接关联v2母网格。独立水位平面是审查代理，不是水利或可冻结素材。

继承的v2试拼保留在文件中并隐藏；用于v3样段的集合为`06_CONNECTED_TRANSITION_FIXTURE__NOT_WORLD`，新母件在`05_TRANSITION_MASTERS__CANDIDATES`。22旧母件+678旧实例的实际网格/UV/材质引用/对象矩阵指纹与v2一致，临时打开隐藏集合再刷新依赖图后读取，避免未求值矩阵产生误判。原角色仅平移，9个对象另作几何/骨架/权重保全比较。

意图明确的交叠：桥梁埋入岸台；相邻桥板平接；柱底埋入桥板/石阶；岩底进入土裙；阶侧土基与实心石阶局部交叠。该做法不豁免独立接缝和承托检查。

## 重建与审查

输入必须使用冻结v2：`AetherLab_CoreKit_Interface_v2.blend`，SHA-256 `9da7bdfc2731c144cb31d0866b9574a24d9f9057e6ed38895226d6371e3ac62b`；加旧源`SCN01_TwoRoofs_Final.blend`，哈希见`docs/Transition_Manifest.json`。恢复包包含可直接重开的最终v3；为控制体积，不重复打包两份大输入。

```sh
blender -t 2 -b INPUT_V2.blend --python scripts/build_transitions.py -- --source-scene SCN01_TwoRoofs_Final.blend --output-root OUTPUT
blender -t 2 -b OUTPUT/source/AetherLab_CoreKit_Transitions_v3.blend --python scripts/independent_review.py -- --output-report OUTPUT/docs/Independent_Geometry_Review.json
blender -t 4 -b OUTPUT/source/AetherLab_CoreKit_Transitions_v3.blend --python scripts/render_transitions.py -- --output-root OUTPUT
```

真实几何结论见独立报告与中文审查。验证不以生成器预设坐标自比较；负控会在内存抬高一块桥板后用相同算法检测，恢复且不覆盖源文件。

## 下一步与明确未做

按[六区覆盖表](../../../../Docs/Art/Global-blockout-coverage-v3.zh-CN.md)开始全局基础布局与十二场景功能粗模，缺少的专用锚点先用清晰可分离体块。大广场、武馆、大厅等按设计净尺寸铺连续地坪，不强迫用4m格拼。

未完成：六区全图、全部25族、建筑/地标/机关成套生产、破坏与绳锚点状态、可变宽度/跨度/转向/高差完整系列、最终原画材质/LOD。v2灯母件既有退化面/UV问题仍保留，不在本批使用。未作UE、导航、碰撞、物理、结构承载、动态通行或性能验收。Windward和银发角色均未编辑。

二进制通过同一Library身份延续版本；最终版本与哈希见交付清单。Git仅存文字源、制作/审查脚本与JSON证据。
