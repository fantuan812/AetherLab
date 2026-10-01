"""Idempotent final source metadata and explicit pickup placement. No engine operation."""
import bpy,os,json,hashlib,argparse,sys,collections
p=argparse.ArgumentParser();p.add_argument('--root',required=True);a=p.parse_args(sys.argv[sys.argv.index('--')+1:]);R=os.path.abspath(a.root);s=bpy.context.scene
for i,n in enumerate(['SCN01_PROP03_WaterBag_Placeholder','SCN01_PROP21_MedSupply_Placeholder']):bpy.data.objects[n].location=(-68.2+i*.4,-343,17.3)
for o in s.objects:
 if '_PROP' in o.name:
  try:o['prop_id']='PROP_'+o.name.split('_PROP')[1][:2]
  except Exception:pass
s.cycles.use_denoising=False
s['intentional_layering']='Geographic route surfaces lifted 6–15mm above nominal floors, with stable per-route offsets, avoiding coplanar shadow artifacts. Not final curb/detail treatment.'
bpy.context.view_layer.update();bpy.ops.wm.save_as_mainfile(filepath=bpy.data.filepath,compress=True)
f=bpy.data.filepath;mf=R+'/docs/World_Manifest.json';d=json.load(open(mf));d['source_sha256']=hashlib.sha256(open(f,'rb').read()).hexdigest();d['source_size_bytes']=os.path.getsize(f);d['postbuild_corrections']=['Pickup proxies placed on crate top outside the 0.8m shelter entry envelope','Stable PROP metadata attached by established ID','Deterministic 6–15mm route layering, as recorded on every route surface'];d['stage']='geometry-and-visual-review candidate';json.dump(d,open(mf,'w'),ensure_ascii=False,indent=2)
stats={'source_sha256':d['source_sha256'],'visible_reuse_instances':len(d['instances']),'by_master':dict(collections.Counter(x['master'] for x in d['instances'])),'used_reuse_master_count':len(set(x['master'] for x in d['instances'])),'preserved_kit_masters':28,'source_derived_masters':len(d['reused_sources']),'specialist_placeholder_count':d['specialist_placeholder_count'],'count_boundaries':'Instance counts are object reuse, not draw calls, engine performance, final asset family completion, or total file mesh counts.'};json.dump(stats,open(R+'/docs/Reuse_Summary.json','w'),ensure_ascii=False,indent=2)
print('FINAL_SOURCE',d['source_sha256'],d['source_size_bytes'],flush=True)
