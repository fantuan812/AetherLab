"""Package final v3 source + readable evidence; no backup, logs or large binary Git payload."""
from pathlib import Path
from PIL import Image
import argparse,hashlib,json,zipfile,shutil,re,os
from urllib.parse import quote
p=argparse.ArgumentParser();p.add_argument('--root',required=True);a=p.parse_args();root=Path(a.root).resolve();out=root/'deliverables';stage=root/'package_stage';stage.mkdir(exist_ok=True)
for sub in ['source','docs','previews','scripts','planning']:(stage/sub).mkdir(exist_ok=True)
shutil.copy2(out/'source/AetherLab_CoreKit_Transitions_v3.blend',stage/'source/AetherLab_CoreKit_Transitions_v3.blend')
for x in (out/'docs').glob('*'):
 if x.is_file():shutil.copy2(x,stage/'docs'/x.name)
for x in (root/'repo/Art/AetherLab/Kits/SharedCore_v3/scripts').glob('*.py'):shutil.copy2(x,stage/'scripts'/x.name)
for x in ['Scene-asset-matrix.zh-CN.md','Scene-asset-matrix.json','Scene-production-standards.zh-CN.md','Global-blockout-coverage-v3.zh-CN.md']:
 src=root/'repo/Docs/Art'/x
 if src.suffix=='.md':
  def link(m):
   target=m.group(1)
   if target.startswith(('https:','http:','#')):return m.group(0)
   file,sep,frag=target.partition('#');relative=os.path.relpath((src.parent/file).resolve(),root/'repo')
   if relative.startswith('..'):return m.group(0)
   return '](https://github.com/fantuan812/AetherLab/blob/art/terrain-transition-kit-v3/'+quote(relative,safe='/')+('#'+frag if sep else '')+')'
  (stage/'planning'/x).write_text(re.sub(r'\]\(([^)]+)\)',link,src.read_text()))
 else:shutil.copy2(src,stage/'planning'/x)
for x in (out/'previews').glob('*.png'):
 if x.name=='CoreKit_Overview.png':shutil.copy2(x,stage/'previews'/x.name)
 else:Image.open(x).convert('RGB').save(stage/'previews'/(x.stem+'.jpg'),quality=87,optimize=True)
readme='''# AetherLab 共享核心地形过渡 v3\n\n先打开source/AetherLab_CoreKit_Transitions_v3.blend；此文件含28个母件（22继承+6新增），77个v3组合样段实例，另保留隐藏v2试拼。\n\n这是34m连续连接样段，不是六区全图。新女主目标1.65m只用标尺，旧1.8028m角色是未缩放历史尺度参照。\n\npreviews有总览和阶/桥/岩/侧面图；docs有独立重开几何、负控、元数据、五图实看与角色保全；planning列六区12场景下一步可复用基础、必须占位空间与未做项目。\n\n不必先精雕所有素材即可开始有条件的全局粗模，但每个新连接仍须实际核对。未完成全图、完整25族、可落桥或绳锚点状态、材质/LOD或UE导航/碰撞/性能验收。4m仅此批候选，不是全项目格网。\n\n旧v2输入与SCN01原始源为重建脚本输入，为控制包体未重复放入。无需输入也能直接重开这里的最终v3文件。完整执行说明见Kit_README.zh-CN.md。\n\n文件沿用原核心包Library身份；旧版本可追溯。Package_Manifest.json列出本包每项内容的SHA-256；它不将自己列入自身哈希。\n'''
(stage/'START_HERE.zh-CN.md').write_text(readme)
# Rewrite local documentation links for the portable tree; preserve repository source separately.
text=(root/'repo/Art/AetherLab/Kits/SharedCore_v3/README.zh-CN.md').read_text().replace('../../../../Docs/Art/Global-blockout-coverage-v3.zh-CN.md','planning/Global-blockout-coverage-v3.zh-CN.md')
(stage/'Kit_README.zh-CN.md').write_text(text)
files=sorted(x for x in stage.rglob('*') if x.is_file() and x.name!='Package_Manifest.json')
manifest={'stage':'v3 minimum transitions','source_sha256':hashlib.sha256((stage/'source/AetherLab_CoreKit_Transitions_v3.blend').read_bytes()).hexdigest(),'members':[{'path':str(x.relative_to(stage)),'bytes':x.stat().st_size,'sha256':hashlib.sha256(x.read_bytes()).hexdigest()} for x in files]}
(stage/'Package_Manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2))
zip_path=out/'AetherLab_SceneAssets_CoreKit_v3.zip'
with zipfile.ZipFile(zip_path,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
 for x in sorted(stage.rglob('*')):
  if x.is_file():z.write(x,str(x.relative_to(stage)))
assert zip_path.stat().st_size<14*1024*1024
with zipfile.ZipFile(zip_path) as z:
 assert z.testzip() is None
 for x in manifest['members']:assert hashlib.sha256(z.read(x['path'])).hexdigest()==x['sha256']
rep={'filename':zip_path.name,'bytes':zip_path.stat().st_size,'mib':zip_path.stat().st_size/1024**2,'sha256':hashlib.sha256(zip_path.read_bytes()).hexdigest(),'verified_content_members':len(manifest['members']),'zip_entries':len(manifest['members'])+1,'limit_mib':14,'passed':True}
(out/'Package_Verification.json').write_text(json.dumps(rep,indent=2));print(json.dumps(rep))
