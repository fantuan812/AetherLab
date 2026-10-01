"""Create size-bounded recovery ZIP with lossless primary views and compact supplementary views."""
from pathlib import Path
import json,hashlib,zipfile,shutil,argparse
p=argparse.ArgumentParser();p.add_argument('--root',required=True);a=p.parse_args();root=Path(a.root).resolve();repo=root/'repo';d=root/'deliverables';kit='Art/AetherLab/Kits/SharedCore_v2';stage=root/'package_stage'
# A new staging folder prevents accidental inclusion of Blender backups or renderer caches.
if stage.exists():shutil.rmtree(stage)
(stage/kit).mkdir(parents=True)
for f in (repo/kit).rglob('*'):
 if f.is_file() and f.suffix in ['.md','.json','.py'] and f.name!='Delivery_Manifest.json':
  target=stage/kit/f.relative_to(repo/kit);target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(f,target)
for f in (d/'docs').glob('*'):
 if f.suffix in ['.json','.md'] and f.name not in ['Delivery_Manifest.json','Package_Manifest.json']:
  target=stage/kit/'docs'/f.name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(f,target)
for f in (root/'scripts').glob('*.py'):
 if f.name=='package_connections.py':continue
 shutil.copy2(f,stage/kit/'source'/f.name)
shutil.copy2(d/'source/AetherLab_CoreKit_Interface_v2.blend',stage/kit/'source/AetherLab_CoreKit_Interface_v2.blend')
for name in ['CoreKit_Overview.png','CoreKit_Assembly_Close.png','CoreKit_Road_Top.jpg','CoreKit_Junction.jpg','CoreKit_Height_Side.jpg','Independent_Road_X_Top.jpg']:
 target=stage/kit/'previews'/name;target.parent.mkdir(exist_ok=True);shutil.copy2(d/'previews'/name,target)
for name in ['Scene-asset-matrix.json','Scene-asset-matrix.zh-CN.md','Scene-production-standards.zh-CN.md']:
 target=stage/'Docs/Art'/name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(repo/'Docs/Art'/name,target)
# The metadata originally describes lossless renderer output. Compact delivery copies remain explicitly identified.
pc=stage/kit/'docs/Preview_Cameras.json';records=json.loads(pc.read_text())
for row in records:
 row['package_filename']=row['filename'] if row['filename'] in ['CoreKit_Overview.png','CoreKit_Assembly_Close.png'] else row['filename'].replace('.png','.jpg')
 row['rendered_png_sha256']=hashlib.sha256((d/'previews'/row['filename']).read_bytes()).hexdigest()
pc.write_text(json.dumps(records,ensure_ascii=False,indent=2))
for md in [stage/kit/'docs/Independent_Review.zh-CN.md']:
 text=md.read_text()
 for name in ['CoreKit_Road_Top','CoreKit_Junction','CoreKit_Height_Side','Independent_Road_X_Top']:text=text.replace(name+'.png',name+'.jpg')
 text+='\n\n包内补充视角为原实看PNG的JPEG压缩副本；总览和主近照保留PNG。原始渲染可用脚本重建，检查结论仍对应最终相同几何。\n';md.write_text(text)
(stage/'START_HERE.zh-CN.md').write_text('''# AetherLab SharedCore v2 连接候选恢复包

直接打开 `Art/AetherLab/Kits/SharedCore_v2/source/AetherLab_CoreKit_Interface_v2.blend`。

这是道路、矮墙、围栏连接试拼，不是全地图。22母件、678实例；27组作者重开、22组独立检查以及9个角色对象保全分别记录，不合并为单一通过数。

主预览在同套件目录的 `previews/`。恢复包包含脚本、接口、检查结果、中文审查与全场景需求矩阵。 `.blend` 已内嵌贴图，可独立打开；从零重建另外需要原v1输入（Library同源身份的历史版本0，原SHA见套件README），不把历史源重复塞入此小包。仓库其他设计引用可从GitHub项目取得。

`Package_Manifest.json` 给出包内各成员的实际字节和SHA-256。Library保存后的版本/文件ID回执以仓库SharedCore_v2/docs/Delivery_Manifest.json为准，避免将写入前打包时的计划状态伪装为已保存。

4m是此套件候选节距；1.65m设计高度与现有1.8028m角色差异保留。未做全图、台阶/桥岸/岩地完整系列、UE、LOD、碰撞、导航或性能验收。未用旧灯112个退化四边形（224个三角形）及40个零面积UV面仍为遗留项。
''')
entries=[]
for f in sorted(stage.rglob('*')):
 if f.is_file():entries.append({'path':str(f.relative_to(stage)),'bytes':f.stat().st_size,'sha256':hashlib.sha256(f.read_bytes()).hexdigest()})
manifest={'scope':'SharedCore v2 isolated road/wall/fence connection fixtures; not whole world','library_ids':{'bundle':'libfile_6130d570bcd881918b29e7748d72f8ee','blend':'libfile_3dc1fbb1e36881919c4dce2f9c37a35e','overview':'libfile_0e88c99c97508191a86003faf70875bd','close':'libfile_d90b767a3a9c819184f4f97d67dd30e4'},'entries':entries,'self_excluded_from_members':True}
(stage/'Package_Manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2))
zipname=d/'AetherLab_SceneAssets_CoreKit_v2.zip'
with zipfile.ZipFile(zipname,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
 for f in sorted(stage.rglob('*')):
  if f.is_file():z.write(f,str(f.relative_to(stage)))
assert zipname.stat().st_size<14*1024*1024,zipname.stat().st_size
with zipfile.ZipFile(zipname) as z:
 assert z.testzip() is None
 for e in entries:assert hashlib.sha256(z.read(e['path'])).hexdigest()==e['sha256']
result={'path':str(zipname),'bytes':zipname.stat().st_size,'MiB':zipname.stat().st_size/1024/1024,'sha256':hashlib.sha256(zipname.read_bytes()).hexdigest(),'verified_members':len(entries),'all_member_hashes_match':True};(d/'Package_Verification.json').write_text(json.dumps(result,indent=2));print(json.dumps(result))
