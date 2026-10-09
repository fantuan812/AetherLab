# 来源、商业制作与文档核对

- 枪体网格、UV、PBR纹理和脚本：本任务原创生成；未导入第三方枪械网格、贴图、扫描、商标或现成枪模。可供本项目后续商业制作使用，不声称独占权或已完成各地区商标/外观权法律清查。
- 参考图：本任务 image_gen 生成的原创虚构概念，不是网络素材、照片、现实武器说明书或真实制造蓝图。参考图只规定外观风格，实际模型与它仍有细节差距。
- 角色适配参考：用户已有 AetherLab CHR01 资产，仅在其原授权/来源边界内继续使用，不把角色或其既有衣物/贴图重标为本次原创。独立枪体包不需要打包角色来源文件。
- 任何将来导入的第三方内容须单独记录作者、URL、许可证/商业条款、版本与改动，本轮未使用。

## 第一方技术依据（2026-10-09核对）

- Blender 4.3 Smart UV Project：https://docs.blender.org/manual/en/4.3/modeling/meshes/editing/uv.html
  采用按几何角度切分的机械物件UV初始展开，仍需后续人工纹理密度与岛布局优化。
- Blender 4.3 glTF PBR 文档：https://docs.blender.org/manual/es/4.3/addons/import_export/scene_gltf2.html
  英文同页本次读取失败，已核官方西语同版本内容。glTF 金属度/粗糙度管线仅导出识别到的材质节点。本任务使用基础色图、粗糙度图和金属度标量。
- Epic FBX Static Mesh Pipeline：https://dev.epicgames.com/documentation/en-us/unreal-engine/fbx-static-mesh-pipeline-in-unreal-engine
  原点、三角化、LOD同原点、单mesh的SOCKET命名/导出边界及FBX版本要求。此交付使用逐模块FBX，未假称多mesh socket 自动导入；PBR在GLB和源文件中保留，FBX材质需目标引擎确认。
