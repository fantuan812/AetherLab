# AetherLab 全图粗模独立实际几何复核

复核日期：2026-10-01。几何 SHA-256：`b78c6ed07157f1cd2f972b4391416417445382dfd6bfdaf6a18c940bbd6eef6c`。
来源对照 SHA-256：`b78c6ed07157f1cd2f972b4391416417445382dfd6bfdaf6a18c940bbd6eef6c`。

## 结论与放行边界

manifest命名路线的本轮采样未发现规定走行包络异常。附加服务/救援接口另列，不能混同；该结果只支持有限几何检查，不等于全图可玩或UE验收。

本报告不运行UE、不编译、不进行导航、碰撞、物理、交互、性能或正式玩法验收。不把粗模专用占位、材质稳定复用、局部路线采样解释为25素材族完成。像素部分已另行完成25幅实看及三份同源回执校验，见 Independent_Global_Visual_Review.zh-CN.md / Independent_Visual_Review.json；基准室内暗图与技术补图严格分开，不构成正式灯光验收。

## 方法和负控

- 直接重开已保存.blend，使用 evaluated mesh、matrix_world 和实际多边形建立每对象BVH；未执行作者构建器
- manifest中的折线只提供建议探测位置；“有地面/有障碍”均来自真实面交点、最近面距离与闭合网格奇偶内外判断，不以常量相等代替几何
- 隐藏母件、旧源档案、技术标注以及人物站位标尺不参与场景障碍判断
- 路线纵向采样≤0.50m，中心及最大±0.75m三轨，窄支路按其范围收窄（每条偏移见JSON）；每轨0.30m半径、1.803m身高。最大约2.1m中央走行带的有限证据，不能据此宣称整条作者路宽全部净空
- 地面允许与路线设计标高相差≤0.22m，脚下≤0.20m低管线按待运行验证的小台阶处理；这是本次几何检查容差，不是项目全局走行标准
- 大厅另以1°角步进、≤0.20m径向步进检查4.2m宽实际环带
- 每桥30道横剖面、0.05m横向间隔检查中央连通净区间；是离散采样下限
- 负控1：只在查询里移除SCN09下桥承托面，桥中央地面由2m降到0.6m渠底，必须被检出
- 负控2：真实门墩位置必须被障碍检测检出，门洞中心必须不被检出
- 负控3：在查询BVH中添加真实遮水面，池水首交必须变成遮挡物；随后移除，不改场景/源文件
- 正控：驿棚复合网格真实石基可作支承，允许脚下容差内的地面不会误作上身障碍

路线总采样：20934；本轮有几何数据对象：2343。源在各自读取过程中未发生变化：geometry=True，provenance=True。

## 世界与关键空间实测

- 地形拓扑：{'vertices': 43466, 'polygons': 43050, 'connected_components': 1, 'boundary_edges': 830, 'internal_boundary_edges': [], 'nonmanifold_edges_gt2': 0, 'zero_area_polygons': 0}
- 连续主地形实际XY跨度：800×800m；六个地区collection；12个SCN均有实际几何（数量见JSON）
- 南门内侧面间净宽 5m；路面至门楣底净高 5.99319m
- 武馆墙面内边活动尺寸 [24.0, 20.0]m；实际地坪同尺寸；7点顶面高度范围 [5.999989986419678, 5.999989986419678]m
- 武馆角柱对矩形的真实交叠记录：{'SCN04_TrainingHall_TimberPost(-12.0, -10.0)': [0, 0], 'SCN04_TrainingHall_TimberPost(-12.0, 10.0)': [0, 0], 'SCN04_TrainingHall_TimberPost(12.0, -10.0)': [0, 0], 'SCN04_TrainingHall_TimberPost(12.0, 10.0)': [0, 0]}
- 集结小院实际地坪：18×16m
- 钟炉大厅实际XY直径：38×38m；r14.5～18.7m的4.2m宽环带，共7920点，地面失败0、障碍点0
- 大厅主柱3根；独立供水平台2处；后维修路另逐段列在路线结果中
- 中继塔本体11.5m；从平台面到信标顶12m；接地杆2根，掩体4件，入侵路2条

## 路线结果

- C01_Mountain_Main：采样无异常；2235点；标高异常0；障碍 无
- C01_Mountain_Rescue_Bypass：采样无异常；585点；标高异常0；障碍 无
- SCN01_Rescue_Access：采样无异常；72点；标高异常0；障碍 无
- C02_Gate_Plaza：采样无异常；462点；标高异常0；障碍 无
- C03_Plaza_Training：采样无异常；99点；标高异常0；障碍 无
- C04_Plaza_Academy：采样无异常；99点；标高异常0；障碍 无
- C05_Plaza_Shop：采样无异常；156点；标高异常0；障碍 无
- C06_Plaza_Inn：采样无异常；207点；标高异常0；障碍 无
- C07_Town_Forest：采样无异常；1701点；标高异常0；障碍 无
- Forest_Loop_South：采样无异常；882点；标高异常0；障碍 无
- Forest_Loop_North：采样无异常；1035点；标高异常0；障碍 无
- Forest_Bridge：采样无异常；39点；标高异常0；障碍 无
- Forest_Log_Shortcut：采样无异常；399点；标高异常0；障碍 无
- C08_Town_Waterworks：采样无异常；1632点；标高异常0；障碍 无
- WW_West_Permanent_Maintenance：采样无异常；789点；标高异常0；障碍 无
- WW_East_Return：采样无异常；816点；标高异常0；障碍 无
- WW_Entry_Approach：采样无异常；147点；标高异常0；障碍 无
- WW_Lower_Deck：采样无异常；39点；标高异常0；障碍 无
- WW_Middle_Approach：采样无异常；87点；标高异常0；障碍 无
- WW_Upper_Approach：采样无异常；87点；标高异常0；障碍 无
- WW_Upper_Deck：采样无异常；39点；标高异常0；障碍 无
- WW_Control：采样无异常；39点；标高异常0；障碍 无
- WW_Craftsman_Access：采样无异常；57点；标高异常0；障碍 无
- WW_Optional_East_Gate：采样无异常；57点；标高异常0；障碍 无
- C09_Town_Abbey：采样无异常；1359点；标高异常0；障碍 无
- Abbey_Water_Approach：采样无异常；21点；标高异常0；障碍 无
- Abbey_Bridge：采样无异常；39点；标高异常0；障碍 无
- C10_Abbey_Hall：采样无异常；405点；标高异常0；障碍 无
- Abbey_Permanent_West_Bypass：采样无异常；918点；标高异常0；障碍 无
- Abbey_Return_East：采样无异常；516点；标高异常0；障碍 无
- C11_Waterworks_Relay：采样无异常；1107点；标高异常0；障碍 无
- C12_Relay_Abbey：采样无异常；1503点；标高异常0；障碍 无
- Relay_Invasion_North：采样无异常；288点；标高异常0；障碍 无
- Relay_Invasion_East：采样无异常；285点；标高异常0；障碍 无
- Relay_Entry_Link：采样无异常；15点；标高异常0；障碍 无
- Relay_Outer_Ring：采样无异常；1344点；标高异常0；障碍 无
- Relay_Core_South：采样无异常；111点；标高异常0；障碍 无
- Relay_Core_West：采样无异常；111点；标高异常0；障碍 无
- Forest_Fire1_Access：采样无异常；75点；标高异常0；障碍 无
- Forest_Fire2_Access：采样无异常；51点；标高异常0；障碍 无
- Forest_Fire3_Access：采样无异常；99点；标高异常0；障碍 无
- Forest_Shelter_Access：采样无异常；78点；标高异常0；障碍 无
- SCN01_Shelter_Access：采样无异常；48点；标高异常0；障碍 无
- Hall_Rear_Maintenance：采样无异常；801点；标高异常0；障碍 无

## 附加空间证据

- 救援桶底中心及周边13点实际面支承；最大垂直差 9.5367431640625e-06m。坡/落台/原平台局部实面采样在JSON中；不以平台整体最高BBox替代脚点
- 原驿棚0.8m人物净包络入口：0.10m步进83点，异常0；复用合并网格中的实际石基地坪参与支承查询
- Gate_Cargo_Side：342点，异常0
- Academy_Double_Aisle：186点，异常0
- Courtyard_Follower：210点，异常0
- Hall_West_Water_To_Ring：111点，异常0
- Hall_East_Water_To_Ring：111点，异常0
- SCN01_RescueRamp_Access：41点，异常0
- SCN08_MaintenanceBridge：30剖面中最小中央净区间采样下限3.3m，中心线失效0
- SCN09_LowerBridge：30剖面中最小中央净区间采样下限3.3m，中心线失效0
- SCN09_UpperBridge：30剖面中最小中央净区间采样下限3.3m，中心线失效0
- SCN10_WaterBridge：30剖面中最小中央净区间采样下限3.3m，中心线失效0

水体可见性与池岸：
- SCN09_PoolWater：120个内域垂直射线，首交水面120，地形高于水面0；桥面/水闸/步石/管道的正常遮挡另保留在JSON
- CHANNEL_1_Water：96个内域垂直射线，首交水面83，地形高于水面0；桥面/水闸/步石/管道的正常遮挡另保留在JSON
- CHANNEL_2_Water：96个内域垂直射线，首交水面89，地形高于水面0；桥面/水闸/步石/管道的正常遮挡另保留在JSON
- CHANNEL_3_Water：96个内域垂直射线，首交水面83，地形高于水面0；桥面/水闸/步石/管道的正常遮挡另保留在JSON
- 池底与实际池岸：{"SCN09_PoolBottom": {"min": [302.0, 15.0, 0.09999999403953552], "max": [316.0, 33.0, 0.6000000238418579], "extent": [14.0, 18.0, 0.5]}, "SCN09_PoolBank302": {"min": [301.75, 15.0, 0.5999999642372131], "max": [302.25, 33.0, 2.0], "extent": [0.5, 18.0, 1.4000000953674316]}, "SCN09_PoolBank316": {"min": [315.75, 15.0, 0.5999999642372131], "max": [316.25, 33.0, 2.0], "extent": [0.5, 18.0, 1.4000000953674316]}, "SCN09_PoolEndBank15": {"min": [302.0, 14.75, 0.5999999642372131], "max": [316.0, 15.25, 2.0], "extent": [14.0, 0.5, 1.4000000953674316]}, "SCN09_PoolEndBank33": {"min": [302.0, 32.75, 0.5999999642372131], "max": [316.0, 33.25, 2.0], "extent": [14.0, 0.5, 1.4000000953674316]}}

- v4 §4.14实际要求南侧入口看北侧供水目标、西侧维护道、东侧可供电门。源入口/目标/渠底/侧门对象实测见JSON；不能把渠闸当作侧门
- 固定维护桥检查只证明其保存几何；可落桥的铰链、绳锚和活动包络仍是独立粗模职责，不由本报告证明动力学

视线射线结果（命中目标炉灯自身是正常到达；null代表到指定目标前未遇实体，不代表像素可读性或所有位置可见）：

{
  "south_gate_to_plaza_hearth_first_hit": {
    "object": "SCN02_PROP05_WarmLamp",
    "point": [
      0.0,
      -0.374237060546875,
      1.6002254486083984
    ],
    "distance_m": 82.62577798057802
  },
  "SCN01_entry_to_south_gate": {
    "object": "SCN01_RescueRamp",
    "point": [
      -56.12215042114258,
      -336.2911376953125,
      16.832420349121094
    ],
    "distance_m": 18.14688614319962
  },
  "SCN01_near_sign_to_south_gate": null,
  "SCN01_entry_to_gate_roof_samples": [
    {
      "target": [
        -4,
        -80,
        7.5
      ],
      "first_hit": {
        "object": "SCN03_Gate_Roof",
        "point": [
          -4.076255798339844,
          -80.37310791015625,
          7.513821601867676
        ],
        "distance_m": 279.4671316736673
      }
    },
    {
      "target": [
        0,
        -80,
        7.5
      ],
      "first_hit": {
        "object": "SCN03_Gate_Roof",
        "point": [
          -0.59124755859375,
          -82.70001220703125,
          7.600018501281738
        ],
        "distance_m": 277.9101687146038
      }
    },
    {
      "target": [
        4,
        -80,
        7.5
      ],
      "first_hit": {
        "object": "SCN03_Gate_Roof",
        "point": [
          3.369335174560547,
          -82.70004272460938,
          7.600019454956055
        ],
        "distance_m": 278.78369116283983
      }
    }
  ]
}

## 来源、复用和角色保全

- 冻结v3母件实际比较28件；变更：{}
- 原kit两套试拼合计755对象；保存全图中同名对象：[]；fixture标签：[]
- 实际复用实例1613；非1世界缩放：[]；未共用母件mesh：[]
- 原角色8网格的顶点/面/UV/权重、骨架rest矩阵与局部变换均逐项对照；网格变更{}，骨架一致True，局部变换变更[]
- 未缩放baseline实际总高1.8028m；不是把旧角色改成1.65m主角
- 58个源同名材质比较节点/输入/连接、packed图片SHA/尺寸/色彩空间；有差异[]。导入的图片ID .001后缀不当作内容改动
- 保存重开后的显式UVMap绑定缺层：0；空材质槽：0

- legacy合并母件 15 件独立对照：pass=True；最大顶点距离 0m，最大active UV差 0；面拓扑及逐面材质名逐项对照；逐件结果见来源JSON
- 原驿棚保留拆件 834 件：raw mesh/UV/权重/材质槽及保存变换字段、父级绑定 pass=True；隐藏collection的未求值matrix_world缓存不作为破坏证据

## 仍未证明的范围

- 尚未做连续路径规划或运行角色穿行；离散采样可能遗漏小于采样间隔的极薄障碍，不能冒称碰撞/导航通过
- legacy保全限15份记录中的源对象及其拼合active UV流；未覆盖未使用的原UV层、smooth标志、自定义法线或其他未采用旧源
- 路线之外的任意漫游、全部景观坡面、AI避障和相机轨迹未覆盖
- 地面低管线/轻微接缝只是几何容差内，运行表现需要后续UE验证
- 供水、电气、火、绳索、活动桥和开门仍是明确占位，不等于因果链已实现
- 本报告仅描述所列SHA；后续任何源修改都使此报告成为历史记录，必须重跑再引用

## 可复跑文件

- independent_geometry_review.py：保存重开、BVH路线/尺寸/净空/负控
- independent_geometry_results.json：每条路线与实际异常坐标、完整指标
- independent_source_preservation.py：跨源网格/UV/权重/骨架/材质对照
- independent_source_preservation.json：对照结果及源哈希
- *.log：Blender执行输出

## 可移植复跑命令

Blender Python参数位于 -- 之后；默认可发现邻接source/docs或deliverables目录，也可显式提供路径。

    blender -b --python scripts/independent_geometry_review.py -- --source source/AetherLab_Global_World_Blockout_v1.blend --manifest docs/World_Manifest.json --output review
    blender -b --python scripts/independent_source_preservation.py -- --source source/AetherLab_Global_World_Blockout_v1.blend --manifest docs/World_Manifest.json --kit /path/to/frozen-v3.blend --legacy /path/to/original-SCN01.blend --output review/independent_source_preservation.json
