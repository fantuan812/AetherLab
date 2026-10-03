"""Assemble the bounded independent review from final immutable-source evidence."""
import json,hashlib,ast
from pathlib import Path
r=Path(str(Path(__file__).resolve().parents[1]));d=r/'independent_review';expected='0a55daaac0dec43bf31d93ae2fc90efb42332758f18228b47aceb0c5a6d48c94';sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
load=lambda n:json.load(open(d/n))
src=load('Saved_Source_Review.json');g=load('Geometry_Probe.json');t=load('Terrain_Scope_Probe.json');b=load('Verge_Boundary_Probe.json');renders=load('Render_Evidence_Review.json');scr=load('Script_Review.json')
assert sha(r/'release/source/AetherLab_Global_World_Blockout_v1.blend')==expected
assert src['source_sha256']['after']==g['source_hashes']['after']==t['source_sha256']==b['source_sha256']==expected
assert all(x['after_source']==expected for x in renders['views']) and renders['all_checks_match']
assert src['scope_checks_pass'] and g['source_unchanged'] and t['source_unchanged'] and b['source_unchanged']
assert all(x['zero_area_faces']==0 and x['nonmanifold_edges_gt2']==0 for x in g['candidate']['verge_topology'].values())
assert g['shoulder_face_centers_over_road']['above_by_over_0p01_count']==0
ast.parse((r/'scripts/refine_scene.py').read_text());scr['script_sha256']=sha(r/'scripts/refine_scene.py');scr['source_sha256_reviewed']=expected;scr['observations_zh'].append('最终新增土肩为三角化开放分片，7/26个连通片；不把位置焊点称为单条完整连续带。');scr['observations_zh']=list(dict.fromkeys(scr['observations_zh']));(d/'Script_Review.json').write_text(json.dumps(scr,ensure_ascii=False,indent=2))
readme=(r/'release/docs/README.zh-CN.md').read_text();assert '- 本批内置源SHA256：'+expected in readme
visual=[{'view':'Ridge_Reveal','finding_zh':'原弯道中央黑色悬空裂口已明显消失；可见道路与地形接近，原灌木接地后不再被大面积埋住。'}, {'view':'Gate','finding_zh':'石砌分层、横脊檐瓦、木檐口和两盏灯柱清楚可见；门洞未被饰件缩窄；登记棚屋顶与既有木构风格相符。'}, {'view':'Town_Reveal','finding_zh':'南门与登记棚形成更可读的入口；植被和路肩关系改善。'}, {'view':'Entry','finding_zh':'角色、量尺、救援车与原铺路的空间关系保持；未见本轮明显可见回归。'}, {'view':'South_Overview','finding_zh':'本批主线/绕行局部过渡改善，可见路网、树群及镇区排布保持。'}, {'view':'Global','finding_zh':'全图布局与主要灰盒关系保持；初候选的南部全宽平滑偏移已收窄。全图图像仅作布局与明显回归检查。'}]
remaining=[{'severity':'nonblocking_for_this_art_batch','id':'retained_crossing_grades','detail_zh':'原两条道路网格精确保留，其交会高程差仍在。5cm外含两条实际道路的支撑采样最大下差17.33cm、反向高19.22cm；不能将道路投影外的6.32cm下差当全部绝对高差上限。'}, {'severity':'nonblocking_for_this_art_batch','id':'open_shoulder_patches','detail_zh':'主线/绕行土肩分别为7/26个连通片的开放面网格，不是连续焊接的实体；地形无内部边只适用于底部连续地形。'}, {'severity':'nonblocking_for_this_art_batch','id':'outer_edge_conformance','detail_zh':'外圈近1.5m边界离散采样仍高于地形4.31cm或埋入2.43cm；+4mm仅是顶点覆盖层设计值。'}, {'severity':'nonblocking_for_this_art_batch','id':'bounded_visual_finish','detail_zh':'道路轮廓仍规则，其他区域大量灰盒；16采样渲染保留噪点。该批是局部美术改善，未达到全图最终风格或引擎资产生产验收。'}]
fixed=[{'id':'wide_smoothing_scope','before':'初候选10,261个保留原面在整个南部横带改变平滑标志','after':'最终保留原面平滑标志变化0；新地形几何局限在申报道路走廊'}, {'id':'shoulder_above_existing_road','before':'初候选37个土肩面心高于原道路超过1cm，最高16.29cm','after':'最终同类面心0；仅4个边缘投影命中面心，均低道路约1.2cm'}, {'id':'clipping_degeneracy','before':'中间候选主/绕土肩9/3零面积面，主路肩16条>2面边','after':'最终两土肩均0零面积面、0条>2面边；无单顶点连通片'}, {'id':'overbroad_route_clipping','before':'中间候选裁切/贴高涉及其他路线，救援坡道附近出现高程副作用','after':'最终裁切/贴高限制为两条山路；外圈最大离地恢复为4.31cm'}, {'id':'comment_honesty','before':'外圈注释将+4mm误称压入地形','after':'已说明为非焊接覆盖层；最终报告明确分片和连续接缝限制'}]
evidence=['Saved_Source_Review.json','Geometry_Probe.json','Terrain_Scope_Probe.json','Verge_Boundary_Probe.json','Render_Evidence_Review.json','Script_Review.json']
report={'review_date_utc':'2026-10-03','conclusion_zh':'本轮Blender轻量独立审查未发现阻塞该局部候选交付的问题；保留列明的接缝、分片与既有交会高程限制。不构成全图最终美术或UE验收。','source_sha256':{'before':src['source_sha256']['before'],'after':expected},'blocking_findings':[],'source_preservation':{k:src[k] for k in ['original_objects_retained','new_object_count','route_count','routes_object_and_mesh_exact','center_count','centers_exact','character_object_count','character_objects_and_meshes_exact','original_armatures_exact','original_materials_exact','original_meshes_removed','foliage_z_only_count','image_relative_path_remaps_only']},'terrain_topology':g['candidate']['terrain_topology'],'terrain_scope':t,'shoulder_topology':g['candidate']['verge_topology'],'foliage_contact':g['foliage'],'gate':g['gate'],'registration_roof':g['registration_roof'],'edge_probes_005m':g['edge_probes']['0.05'],'outer_edge_probe':b['outer_ring_approx_1p5m'],'fixed_during_review':fixed,'remaining_findings':remaining,'visual_review':{'all_six_final_views_actually_viewed':True,'all_six_before_views_actually_viewed':True,'camera_and_render_receipts_match':True,'findings':visual},'documentation_review':{'readme_sha256':sha(r/'release/docs/README.zh-CN.md'),'final_source_sha_present':True,'script_sha256':scr['script_sha256'],'ast_parse':True,'note_zh':'本报告与最终JSON包含分层采样，不沿用初候选数值；全地形派生材质换槽属于显式申报，原材质数据未改。'},'evidence':[{ 'file':f,'sha256':sha(d/f)} for f in evidence],'scope_limits_zh':['仅Blender已保存源回读、静态脚本/数据比较、离散几何查询及六机位图像审看；审查不写作者源','未执行UE编译、规则测试、运行测试、导航、碰撞、物理、性能、LOD或光照UV验收','离散点与边界采样不能证明整个连续网格处处无缝；视觉检查仅覆盖所列机位','475植被接地以最低顶点高度对原点XY的地形高度，不代表整个植物脚印或全部叶片贴地','本独立报告对应内置源0a55daa，不代替便携外置贴图转出及ZIP打包完整性的单独核验']}
(d/'Independent_Review.json').write_text(json.dumps(report,ensure_ascii=False,indent=2))
md=f'''# AetherLab 南线路肩与南门候选：独立轻量审查

日期：2026-10-03（UTC）

## 结论

本轮未发现阻塞该局部候选交付的问题。南线路肩和南门的可见改善成立；仍有下述既有交会高程差、外圈接缝与开放分片限制。**这不是全图最终美术、连续碰撞或 UE 验收。未编译、未测试（UE 项目）。**

- 基线内置源：`{src['source_sha256']['before']}`
- 最终内置源：`{expected}`
- 作者脚本：`{scr['script_sha256']}`

## 实际核对

- 独立重开两份保存源，比较对象、网格、变换、材质节点、骨架与贴图字节。44 条道路对象及网格、12 个中心、角色 10 对象和骨架精确保留；原对象无删除；四个门楼网格替换符合申报。两张内置图仅相对路径随保存位置重映射，packed 字节/尺寸/色彩空间不变。
- 475 个原植被对象仅改变 Z；XY、旋转、缩放与母网格保留。原点 XY 处最低顶点对地形高度误差小于 0.000001m。这不是整个植物脚印贴地证明。
- 地形为 53,444 点/52,836 面，1 个连通分量；没有内部开口边、>2 面边或零面积面。原 43,466 个 XY 全保留；新增 9,978 个 XY，范围 X[-96,16]、Y[-380,-88]m。原点中 644 个 Z 改变，范围 X[-92,12]、Y[-380,-88]m。作者所说的 10,177 是细分后调整点数，不能混同原始点数。
- 初候选南部全宽平滑越界已修正：最终匹配的原保留面额外平滑变化为 0。全地形面引用新的派生材质是明确的全对象换槽，新增颜色影响由局部属性控制；原材质数据未修改。
- 两土肩均无零面积面、>2 面边；裁后主线 7 个、绕行 26 个连通片，不是两条完整连续焊接带。覆原路面且高于它超过 1cm 的面心采样由 37 降至 0。
- 门柱、梁、屋顶原包围盒保持。结构开口宽 5m、梁下 Z=6m；3,111 条门洞竖向射线未见新增饰件侵入 5.99m 以下。实际行走净高还应扣除路面高度，作者几何报告约 5.993m。登记棚新屋顶直接共享 SRC_Roof_H03，单位缩放，最低顶点 Z=3.4m。

## 接缝数值与限制

独立边缘采样沿两条山路实际边界，以≤0.20m间距的中间点采样，Y(-372,-96)m；每档 4,076 点。正值表示路边高于支撑，负值表示支撑高于路边。

- 外 5cm，裸地形范围 -9.53～21.55cm；地形加土肩范围 -16.82～21.55cm。
- 加入两条实际道路表面后，外 5cm 平均下差 1.17cm，范围 -19.22～17.33cm。交会处原道路高程本身不同，保护原网格的前提下不能用土肩盖住该差异。
- 仅看不落在两路投影内的 3,885 点，外 5cm 平均下差 1.31cm，最大下差 6.32cm，反向高 16.82cm。**6.32cm 不是全部接缝的绝对上限。**
- 土肩实际外边界、距路约 1.4～1.6m 的离散探针中，最大离地 4.31cm、最大埋入 2.43cm。脚本 +4mm 只是顶点覆盖层设计，面内/边中点不保证同值。
- 土肩是开放分片且允许与接收地形覆盖相交；地形“无内部开口边”的结论不能推广成全部土肩无缝。未做连续扫掠、导航或引擎碰撞验证。

## 六组真实图像

已经逐张查看最终源的 Ridge_Reveal、Gate、Entry、Town_Reveal、South_Overview、Global，并与基线六图比较。逐图像素 SHA、相机矩阵、镜头/正交参数、分辨率和 Cycles 16 采样与收据相符；原灯光数据保持，新增灯柱发光属于本轮资产变化。

Ridge_Reveal 原黑色悬空裂口明显消失；Gate 的石砌、横脊瓦檐、灯柱与登记棚顶可读性提高；Entry 的角色/量尺/救援关系保持；鸟瞰与全图未见额外布局回归。道路仍规则、其他区域仍大量灰盒，渲染有采样噪点；六图不代表全图最终品质。

## 脚本与证据审阅

作者脚本完成静态读取和 AST 解析，未由本审查者执行制作脚本；独立回读脚本不导入作者逻辑、不保存 blend。局部走廊细分、两山路边界派生肩、近同层切边过渡、微小退化片清理、475 对象 Z 调整与门楼白名单改动均与最终源对照一致。初候选越界平滑、覆路肩片以及中间候选退化拓扑已修复；外圈覆盖层与开放分片限制如实保留。

详细数据：`Independent_Review.json`、`Saved_Source_Review.json`、`Geometry_Probe.json`、`Terrain_Scope_Probe.json`、`Verge_Boundary_Probe.json`、`Render_Evidence_Review.json`、`Script_Review.json`。

本报告只绑定上述内置源；便携外置贴图源与 ZIP 包需以各自的单独核验记录为准。
'''
(d/'Independent_Review.zh-CN.md').write_text(md)
print('WRITTEN',d/'Independent_Review.json',d/'Independent_Review.zh-CN.md')
