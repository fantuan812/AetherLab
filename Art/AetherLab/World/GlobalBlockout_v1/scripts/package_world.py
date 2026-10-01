"""Create three ordinary independent ZIP files, each below 14 MiB. No split ZIP format."""
import pathlib,json,hashlib,zipfile,shutil,argparse
from PIL import Image
p=argparse.ArgumentParser();p.add_argument('--root',required=True);a=p.parse_args();R=pathlib.Path(a.root).resolve();D=R/'deliverables';P=R/'package_stage/AetherLab_Global_Blockout_v1';O=D/'downloads';O.mkdir(exist_ok=True);(P/'docs').mkdir(exist_ok=True);(P/'scripts').mkdir(exist_ok=True);(P/'previews').mkdir(exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();world=json.loads((D/'docs/World_Manifest.json').read_text());source_sha=world['source_sha256'];portable=json.loads((P/'docs/Portable_Manifest.json').read_text());assert portable['frozen_source_sha256']==source_sha and portable['reopen_verified'];assert sha(P/portable['portable_file'])==portable['portable_sha256']
rows=[]
for file in ['Render_Receipt_all.json','Render_Receipt_TechFill.json','Render_Receipt_TechWide.json']:
 rec=json.loads((D/'previews_release'/file).read_text());assert rec['source_unchanged'] and rec['source_sha256_at_start']==source_sha==rec['source_sha256_at_end']
 for v in rec['views']:
  im=D/'previews_release'/v['file'];assert sha(im)==v['sha256'] and v['source_sha256']==source_sha;rows.append(v)
 shutil.copyfile(D/'previews_release'/file,D/'docs'/file)
assert len(rows)==25 and len({v['file'] for v in rows})==25
conv=[]
for r in rows:
 src=D/'previews_release'/r['file'];out=P/'previews'/(src.stem+'.jpg')
 with Image.open(src) as im:im.convert('RGB').save(out,'JPEG',quality=86,subsampling=0,optimize=True);size=list(im.size)
 conv.append({'file':'previews/'+out.name,'sha256':sha(out),'source_png':src.name,'source_png_sha256':r['sha256'],'size_px':size,'conversion':'JPEG quality86, chroma subsampling0; resolution unchanged; for compact preview delivery'})
(D/'docs/Preview_Conversion.json').write_text(json.dumps({'frozen_source_sha256':source_sha,'files':conv},ensure_ascii=False,indent=2))
for f in (D/'docs').iterdir():
 if f.is_file() and f.suffix in ['.md','.json','.txt'] and f.name not in ['Delivery_Manifest.json','Library_Delivery_Receipt.json','Package_Verification.json','Package_Extraction_Validation.json']:
  shutil.copyfile(f,P/'docs'/f.name)
scripts=['build_global_blockout.py','finalize_world.py','render_world.py','render_technical_interiors.py','write_delivery_docs.py','make_portable_copy.py','verify_portable_copy.py','package_world.py']
for n in scripts:shutil.copyfile(R/'scripts'/n,P/'scripts'/n)
for n in ['independent_geometry_review.py','independent_source_preservation.py']:shutil.copyfile(R/'review'/n,P/'scripts'/n)
shutil.copyfile(D/'START_HERE.zh-CN.txt',P/'START_HERE.zh-CN.txt')
(P/'TEXTURES_README.zh-CN.txt').write_text('这是普通独立ZIP的贴图包，不是分卷。请与模型包解压到同一目录，保留source/textures路径。5个原图像ID使用3份唯一PNG；原始字节、1254×1254尺寸与sRGB色彩空间不变。\n')
(P/'PREVIEWS_README.zh-CN.txt').write_text('这是普通独立ZIP的预览包，可以单独解压。25张JPEG包括21基准图、3技术补光图、1术院24mm技术广角图。基准武馆/商店图仍暗，请使用带TechFill文件；三材料同框用TechWide。技术补光没有写回保存源，不是正式灯光验收。完整尺寸未缩小，仅转JPEG便于下载。\n')
files=sorted(x for x in P.rglob('*') if x.is_file() and not x.name.endswith('.blend1') and x.name!='Package_Manifest.json')
pm={'package_type':'three independent ordinary ZIP archives, not multipart volumes','canonical_frozen_source_sha256':source_sha,'portable_source_sha256':portable['portable_sha256'],'files':[{'file':str(f.relative_to(P)),'bytes':f.stat().st_size,'sha256':sha(f)} for f in files]};(P/'docs/Package_Manifest.json').write_text(json.dumps(pm,ensure_ascii=False,indent=2));shutil.copyfile(P/'docs/Package_Manifest.json',D/'docs/Package_Manifest.json')
model=[P/'source/AetherLab_Global_World_Blockout_v1.blend',P/'START_HERE.zh-CN.txt']+sorted((P/'docs').glob('*'))+sorted((P/'scripts').glob('*.py'))
textures=sorted((P/'source/textures').glob('*.png'))+[P/'TEXTURES_README.zh-CN.txt'];views=sorted((P/'previews').glob('*.jpg'))+[P/'PREVIEWS_README.zh-CN.txt'];out=[]
for name,items in [('Model',model),('Textures',textures),('Views',views)]:
 path=O/('AetherLab_Global_Blockout_'+name+'_v1.zip')
 with zipfile.ZipFile(path,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
  for f in items:
   info=zipfile.ZipInfo(str(f.relative_to(P.parent)),date_time=(2026,10,1,0,0,0));info.external_attr=0o644<<16;z.writestr(info,f.read_bytes(),compress_type=zipfile.ZIP_DEFLATED,compresslevel=6)
 assert path.stat().st_size<14*1024*1024,(name,path.stat().st_size)
 with zipfile.ZipFile(path) as z:assert z.testzip() is None
 out.append({'file':path.name,'bytes':path.stat().st_size,'sha256':sha(path),'entries':len(items),'ordinary_independent_zip':True})
V=R/'recovered_local';V.mkdir(exist_ok=True)
for r in out:
 with zipfile.ZipFile(O/r['file']) as z:z.extractall(V)
errors=[]
for f in pm['files']:
 q=V/P.name/f['file']
 if not q.is_file() or sha(q)!=f['sha256']:errors.append(f['file'])
assert not errors,errors
receipt={'archives':out,'all_below_14MiB':True,'ordinary_independent_zips':True,'crc_and_extracted_file_hashes_passed':True,'extracted_root':P.name,'checked_content_files':len(pm['files']),'portable_blend_sha256':portable['portable_sha256'],'canonical_frozen_blend_sha256':source_sha,'blender_reopen_after_zip_extraction':'pending'}
(D/'docs/Package_Verification.json').write_text(json.dumps(receipt,ensure_ascii=False,indent=2));print(json.dumps(out,ensure_ascii=False,indent=2))
