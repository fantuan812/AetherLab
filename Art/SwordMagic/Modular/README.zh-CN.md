# 模块化剑魔原型

打开 `BrokenBellAbbey_Modular.blend`。关卡在 `SM_01_BrokenBellAbbey`，拆分后的身体与装备在 `SM_03_ModularCharacters`；原始角色展示场景也保留。装备导出后在第三个场景中默认隐藏，可在 Outliner 取消隐藏单独编辑。

`FBX` 中的角色名称以 SK_Modular 开头，静态武器以 SM 开头，动画以 AN 开头。装备以手骨为局部原点；不要合并回身体。两套 17 骨原型骨架分别用于游誓者和铸钟骑士。

UE 运行入口：项目根目录的 `Scripts/PlayAdventure.ps1`。R 切换剑/训练锤，T 装卸盾牌，X 卸主手。剑盾与锤使用同一角色。

重新导入：`Scripts/ImportSwordMagic.ps1`。该脚本会刷新生成的 `/Game/SwordMagic` 内容和数据配置；要保留手工修改，请先复制到其他资产路径。`-DataOnly` 复用网格和动画并更新数据、材质及本轮修正的学徒交互件。

源建模脚本：`Tools/BlenderMCP/prepare_modular_assets.py`；桥柱遮挡之外的学徒摆放修正通过 `fix_apprentice_placement.py` 在 Blender MCP 中执行。MCP 使用与模型变换相关的代码，UE 导入由引擎编辑器自身的 Python API 完成。

详见项目 `Docs/ModularGameplay.zh-CN.md`。此包是可玩原型，双手 IK、正式蒙太奇、面部与布料待制作。`manifest.json` 是 Blender 导出清单；`unreal-import-report.json` 是 UE 导入结果。
