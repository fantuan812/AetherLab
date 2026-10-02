# AetherLab 六区十二场景连续世界粗模 v1

完成一个800×800m连续世界的灰盒布局里程碑；仍不是最终环境美术或UE可玩地图。本轮到此停止，不继续局部精修。

- 完整冻结源：`b78c6ed07157f1cd2f972b4391416417445382dfd6bfdaf6a18c940bbd6eef6c`
- 外置贴图恢复副本：`007c21056ce98b31fd4f4319bde214a486f02edc49c13007970d1b0b1c5f31f5`，只改变贴图内置/相对路径，几何/UV/图像字节另经重开验证
- 1613处实例复用24份实际使用母网格；保留28 kit母件/15旧源母件，专用占位102项，其他建筑/地形/路线体块另计
- 独立44命名路线、6附加访问、4.2m大厅环带、固定桥、真实池水/四岸、救援平台与桶底接触检查；来源保全与负控见报告
- 21基准图加4张明确标注的技术补光/广角图均已实看。武馆/店面基准照明仍暗，不把补光写成正式灯光通过
- 画面仍稀疏、地形块状，长路中间视线与事件节奏、专用资产、材质/LOD和局部细化留在后续

## 交付

三个普通独立ZIP，不是分卷：模型、原字节贴图、25图预览。每包小于14MiB；模型和贴图解压到同一目录，保留共同的`AetherLab_Global_Blockout_v1/source/textures`结构，再打开source中的.blend。完整大源另存Library新全图身份，未覆盖核心套件身份。

[完整使用与范围说明](docs/README.zh-CN.md) · [独立几何/来源审查](docs/Independent_Review.zh-CN.md) · [独立像素审查](docs/Independent_Global_Visual_Review.zh-CN.md) · [包校验](docs/Package_Verification.json) · [解压重开](docs/Package_Extraction_Validation.json) · [最终Library交付清单](docs/Delivery_Manifest.json)

未编译、未测试（UE项目）。本批仅开展Blender制作、保存重开、几何/来源/像素检查；不声明引擎导航、碰撞、物理交互或运行性能通过。
