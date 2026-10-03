"""Assemble independently reopenable model and truthful fixed-camera previews."""
from pathlib import Path
import json,hashlib,shutil,zipfile,ast
from PIL import Image,ImageDraw,ImageFont
r=Path(__file__).resolve().parents[1];release=r/'release';portable=r/'portable/AetherLab_Global_Blockout_v1';views=r/'views/AetherLab_Global_Blockout_Views_v1';delivery=r/'delivery';delivery.mkdir(exist_ok=True);views.mkdir(parents=True,exist_ok=True)
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest();current=sha(release/'source/AetherLab_Global_World_Blockout_v1.blend');old=sha(r/'input/AetherLab_Global_World_Blockout_v1.blend');receipts={};index={}
for side,folder,expected in [('Before',r/'before',old),('After',r/'release_after',current)]:
 selected=[json.loads(p.read_text()) for p in folder.glob('Render_Receipt_*.json') if json.loads(p.read_text())['source_sha256_at_start']==expected]
 rows={}
 for rec in selected:
  assert rec['source_unchanged'] and rec['source_sha256_at_end']==expected
  for row in rec['views']:
   assert sha(folder/row['file'])==row['sha256'];rows[row['file']]=row
 assert len(rows)==6,(side,len(rows));receipts[side]={'source_sha256':expected,'views':list(rows.values())};index[side]=rows;(views/side).mkdir(exist_ok=True)
 for name,row in rows.items():Image.open(folder/name).convert('RGB').save(views/side/(Path(name).stem+'.jpg'),quality=90,subsampling=0)
for name,b in index['Before'].items():
 a=index['After'][name]
 for key in ['matrix_world','lens_mm','ortho_scale','resolution','samples']:assert b[key]==a[key],(name,key)
font=ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',26)
for name in ['Ridge_Reveal','Gate']:
 b=Image.open(r/'before'/f'{name}.png').convert('RGB');a=Image.open(r/'release_after'/f'{name}.png').convert('RGB');w,h=b.size;assert a.size==b.size
 im=Image.new('RGB',(w*2,h+52),(23,29,32));im.paste(b,(0,52));im.paste(a,(w,52));draw=ImageDraw.Draw(im);draw.text((20,12),'BEFORE | South landscape v2',font=font,fill='white');draw.text((w+20,12),'AFTER | Road shoulder + gate v3',font=font,fill='white');im.save(views/f'{name}_Before_After.jpg',quality=92,subsampling=0)
(release/'docs/Render_Receipts.json').write_text(json.dumps(receipts,indent=2))
# Copy only this frozen candidate's evidence. Large transient raw snapshots and
# predecessor candidate data are intentionally not presented as final results.
for p in (release/'docs').glob('*'):shutil.copy2(p,portable/'docs'/p.name)
shutil.copy2(release/'review/independent_geometry_results.json',portable/'docs/Geometry_Results.json')
shutil.copy2(r/'AetherLab_Global_Blockout_v1/docs/World_Manifest.json',portable/'docs/Baseline_World_Manifest.json')
(portable/'scripts').mkdir(exist_ok=True)
for name in ['refine_scene.py','check_shoulders.py','compare_scene_snapshots.py','package_scene.py']:
 shutil.copy2(r/'scripts'/name,portable/'scripts'/name)
for name in ['independent_saved_snapshot.py','independent_geometry_review.py','make_portable_copy.py','verify_portable_copy.py','render_refinement.py']:
 shutil.copy2(r/'AetherLab_Global_Blockout_v1/scripts'/name,portable/'scripts'/name)
for p in (r/'independent_review').glob('*.py'):shutil.copy2(p,portable/'scripts'/('reviewer_'+p.name))
for name in ['Independent_Review.zh-CN.md','Independent_Review.json','Geometry_Probe.json','Terrain_Scope_Probe.json','Saved_Source_Review.json','Verge_Boundary_Probe.json','Script_Review.json','Render_Evidence_Review.json']:
 p=r/'independent_review'/name
 if p.exists():shutil.copy2(p,portable/'docs'/name)
syntax=[]
for p in (portable/'scripts').glob('*.py'):ast.parse(p.read_text());syntax.append({'file':p.name,'sha256':sha(p),'ast_parse':True})
(portable/'docs/Script_Syntax_Check.json').write_text(json.dumps({'scripts':syntax,'limits':'Python AST only; not UE compile or project testing'},indent=2))
(portable/'START_HERE.zh-CN.txt').write_text('南线路肩与南门局部细化v3。模型包和原贴图包解压到同一目录，打开source/AetherLab_Global_World_Blockout_v1.blend。先读docs/README.zh-CN.md与独立报告。六区十二场景保持；未编译、未测试UE。\n')
(views/'README.zh-CN.txt').write_text('12张本批同机位真实Blender渲染，Before为PR36，After为本批最终源；另2张对照只拼图加标签。16采样，未生成式补画。灰盒/局部细化范围与限制请读模型包docs。\n')
# Original external texture bytes must match the previous delivered package.
tex=r/'AetherLab_Global_Blockout_v1/source/textures';compat=[]
for p in (portable/'source/textures').glob('*.png'):
 match=sha(p)==sha(tex/p.name);compat.append({'file':p.name,'sha256':sha(p),'unchanged':match});assert match
(portable/'docs/Texture_Compatibility.json').write_text(json.dumps(compat,indent=2))
outputs=[]
for kind,base in [('Model',portable),('Views',views)]:
 out=delivery/f'AetherLab_Global_Blockout_{kind}_v1.zip';files=[]
 with zipfile.ZipFile(out,'w',zipfile.ZIP_DEFLATED,9) as z:
  for p in sorted(base.rglob('*')):
   if not p.is_file() or p.name.endswith('.blend1') or (kind=='Model' and 'textures' in p.parts):continue
   rel=base.name+'/'+str(p.relative_to(base));z.write(p,rel);files.append({'file':rel,'sha256':sha(p),'bytes':p.stat().st_size})
 assert out.stat().st_size<14*1024*1024
 with zipfile.ZipFile(out) as z:assert z.testzip() is None
 outputs.append({'file':out.name,'sha256':sha(out),'bytes':out.stat().st_size,'members':files})
(delivery/'Package_Manifest.json').write_text(json.dumps({'source_sha256':current,'ordinary_zips_under_14MiB':True,'packages':outputs},indent=2));print(json.dumps([{k:v for k,v in x.items() if k!='members'} for x in outputs],indent=2))
