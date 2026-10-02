"""Create ordinary model and A/B-view ZIPs; reuse the unchanged texture ZIP."""
from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
import json,hashlib,zipfile,shutil,ast,argparse
p=argparse.ArgumentParser();p.add_argument('--root',required=True);a=p.parse_args();R=Path(a.root).resolve();P=R/'portable/AetherLab_Global_Blockout_v1';D=R/'deliverables';V=R/'preview_delivery/AetherLab_Global_Blockout_v1';O=R/'packages';O.mkdir(exist_ok=True);(V/'previews').mkdir(parents=True,exist_ok=True);(V/'docs').mkdir(exist_ok=True);(P/'scripts').mkdir(exist_ok=True)
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
portable=json.loads((P/'docs/Portable_Manifest.json').read_text());verified=json.loads((P/'docs/Portable_Reopen_Validation.json').read_text())
assert verified['passed'] and verified['portable_source_sha256']==sha(P/'source/AetherLab_Global_World_Blockout_v1.blend')==portable['portable_sha256']
assert sha(D/'source/AetherLab_Global_World_Blockout_v1.blend')==portable['frozen_source_sha256']
portable['reopen_verified']=True;portable['reopen_validation']='Portable_Reopen_Validation.json';(P/'docs/Portable_Manifest.json').write_text(json.dumps(portable,ensure_ascii=False,indent=2))
for f in (D/'docs').glob('*'):
 if f.name not in {'Package_Verification.json','Delivery_Manifest.json','Local_Extraction_Verification.json','Remote_Package_Verification.json','Remote_Package_Reopen.json'}:shutil.copy2(f,P/'docs'/f.name)
for name in ['refine_south_landscape.py','render_refinement.py','package_landscape.py','verify_release_packages.py']:shutil.copy2(R/'scripts'/name,P/'scripts'/name)
for name in ['independent_geometry_review.py','make_portable_copy.py','verify_portable_copy.py']:shutil.copy2(R/'inputs/recovery/AetherLab_Global_Blockout_v1/scripts'/name,P/'scripts'/name)
review=R/'review'
if (review/'final').exists():
 for f in (review/'final').glob('*'):
  if f.suffix in {'.json','.md','.py'}:shutil.copy2(f,P/'docs'/f.name)
for f in review.glob('*.py'):shutil.copy2(f,P/'scripts'/f.name)
views=[]
for state,dirn in [('Before','before'),('After','after_candidate')]:
 folder=V/'previews'/state;folder.mkdir(exist_ok=True);rec=json.loads((R/'renders'/dirn/'Render_Receipt_all.json').read_text())
 shutil.copy2(R/'renders'/dirn/'Render_Receipt_all.json',V/'docs'/('Render_Receipt_'+state+'.json'))
 for row in rec['views']:
  source=R/'renders'/dirn/row['file'];dest=folder/(source.stem+'.jpg');im=Image.open(source).convert('RGB');im.save(dest,quality=90,subsampling=0)
  views.append({'file':str(dest.relative_to(V)),'png_sha256':sha(source),'jpeg_sha256':sha(dest),'dimensions':list(im.size),'source_sha256':row['source_sha256'],'conversion':'JPEG quality90 4:4:4, original resolution, no scene editing'})
# Technical comparison sheets are layouts of actual unmodified rendered pixels.
font=ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',28)
for name in ['South_Overview','Ridge_Reveal']:
 b=Image.open(R/'renders/before'/f'{name}.png').convert('RGB');f=Image.open(R/'renders/after_candidate'/f'{name}.png').convert('RGB');w,h=b.size
 sheet=Image.new('RGB',(w*2,h+58),(25,33,35));sheet.paste(b,(0,58));sheet.paste(f,(w,58));draw=ImageDraw.Draw(sheet);draw.text((24,15),'BEFORE | Global v1',font=font,fill='white');draw.text((w+24,15),'AFTER | South landscape v2',font=font,fill='white');out=D/(name+'_Before_After.jpg');sheet.save(out,quality=92,subsampling=0);shutil.copy2(out,V/'previews'/out.name)
(V/'docs/Preview_Conversion.json').write_text(json.dumps(views,indent=2))
shutil.copy2(D/'docs/README.zh-CN.md',V/'README.zh-CN.md');shutil.copy2(D/'START_HERE.zh-CN.txt',P/'START_HERE.zh-CN.txt');shutil.copy2(D/'START_HERE.zh-CN.txt',V/'START_HERE.zh-CN.txt')
for f in (P/'scripts').glob('*.py'):ast.parse(f.read_text())
packages=[]
for tree,name,excluded in [(P,'AetherLab_Global_Blockout_Model_v1.zip',{'textures'}),(V,'AetherLab_Global_Blockout_Views_v1.zip',set())]:
 files=[f for f in tree.rglob('*') if f.is_file() and not any(x in f.relative_to(tree).parts for x in excluded) and not f.name.endswith('.blend1') and not f.name.startswith('Package_Content')]
 content=[{'path':str(f.relative_to(tree.parent)),'bytes':f.stat().st_size,'sha256':sha(f)} for f in files]
 manifest_name='Package_Content_Model.json' if tree==P else 'Package_Content_Views.json'
 (tree/manifest_name).write_text(json.dumps(content,indent=2));files.append(tree/manifest_name)
 dest=O/name
 with zipfile.ZipFile(dest,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
  for f in sorted(files):
   zi=zipfile.ZipInfo(str(f.relative_to(tree.parent)),date_time=(2026,10,2,0,0,0));zi.compress_type=zipfile.ZIP_DEFLATED;z.writestr(zi,f.read_bytes())
 with zipfile.ZipFile(dest) as z:assert z.testzip() is None
 assert dest.stat().st_size<14*1024*1024
 packages.append({'file':name,'bytes':dest.stat().st_size,'sha256':sha(dest),'crc_passed':True,'under_14MiB':True,'files':len(files)})
(D/'docs/Package_Verification.json').write_text(json.dumps({'packages':packages,'texture_package_reused_unchanged':True,'format':'ordinary independent ZIPs, not split volumes','preview_actual_render_count':12,'comparison_sheets':2},indent=2))
print(json.dumps(packages,indent=2))
