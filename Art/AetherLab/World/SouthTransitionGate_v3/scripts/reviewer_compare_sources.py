"""Independent comparison of freshly reopened immutable source snapshots."""
import json,hashlib,collections
from pathlib import Path
root=Path(str(Path(__file__).resolve().parents[1]));out=root/'independent_review';sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
def stable(v):
 if isinstance(v,dict):return {k:stable(x) for k,x in v.items() if k!='session_uid'}
 if isinstance(v,list):return [stable(x) for x in v]
 return v
b=stable(json.load(open(out/'snapshot_before.json')));a=stable(json.load(open(out/'snapshot_after.json')))
errors=[];diff=[];zonly=[]
allowed={'WORLD_Terrain_Continuous_800x800m':{'custom','material_slots'},'SCN03_Gate_Lintel':{'data','material_slots'},'SCN03_Gate_Pier-4':{'data','material_slots'},'SCN03_Gate_Pier4':{'data','material_slots'},'SCN03_Gate_Roof':{'data'},'SCN03_Registration_Canopy':{'hide_render','hide_viewport'}}
for n,o in b['objects'].items():
 if n not in a['objects']:errors.append('Removed original object '+n);continue
 x=a['objects'][n];fields={k for k in o if x[k]!=o[k]}
 if fields:
  diff.append({'name':n,'fields':sorted(fields)})
  if fields=={'transform'}:
   p=o['transform'].copy();q=x['transform'].copy();pl=p.pop('location');ql=q.pop('location')
   if p==q and pl[:2]==ql[:2] and o['custom'].get('master') and set(o['collections'])&{'08_CONTEXT_FOLIAGE','12_SOUTH_LANDSCAPE_V2'}:zonly.append(n)
   else:errors.append('Unexpected original transform '+n)
  elif fields!=allowed.get(n):errors.append('Unexpected original object mutation '+n+' '+str(fields))
route_names=[n for n in b['objects'] if n.startswith('ROUTE_')];centers=[n for n in b['objects'] if n.startswith('SCN_') and n.endswith('_CENTER')]
char_names=b['collections']['05_EXISTING_CHARACTER__UNSCALED_65_BONES']['objects']
def exact_object_mesh(n):
 o=b['objects'][n];x=a['objects'].get(n)
 return o==x and (o['type']!='MESH' or b['meshes'][o['data']]==a['meshes'][x['data']])
removed=set(b['meshes'])-set(a['meshes']);expected_removed={'SCN03_Gate_Lintel','SCN03_Gate_Pier-4','SCN03_Gate_Pier4','SCN03_Gate_Roof'}
if removed!=expected_removed:errors.append('Unexpected removed mesh set')
for n,m in b['meshes'].items():
 if n not in expected_removed|{'WORLD_Terrain_Continuous_800x800m'} and m!=a['meshes'].get(n):errors.append('Changed retained mesh '+n)
for group in ['materials','armatures','cameras','lights','curves','node_groups']:
 for n,v in b[group].items():
  if v!=a[group].get(n):errors.append('Changed original '+group+' '+n)
paths=[]
for n,v in b['images'].items():
 x=a['images'].get(n)
 if v!=x:
  vv=v.copy();xx=x.copy();vp=vv.pop('filepath');xp=xx.pop('filepath')
  if vv==xx and vv['packed_sha256']:paths.append({'image':n,'before':vp,'after':xp,'packed_sha256':vv['packed_sha256']})
  else:errors.append('Changed original image '+n)
report={'source_sha256':{'before':b['source_sha256'],'after':a['source_sha256']},'snapshot_sha256':{'before':sha(out/'snapshot_before.json'),'after':sha(out/'snapshot_after.json')},'source_unchanged_while_snapshotted':b['source_unchanged'] and a['source_unchanged'],'runtime_field_ignored':'session_uid only','original_objects_before':len(b['objects']),'original_objects_retained':not(set(b['objects'])-set(a['objects'])),'new_object_count':len(set(a['objects'])-set(b['objects'])),'route_count':len(route_names),'routes_object_and_mesh_exact':all(exact_object_mesh(n) for n in route_names),'center_count':len(centers),'centers_exact':all(exact_object_mesh(n) for n in centers),'character_object_count':len(char_names),'character_objects_and_meshes_exact':all(exact_object_mesh(n) for n in char_names),'original_armatures_exact':b['armatures']==a['armatures'],'original_materials_exact':all(v==a['materials'].get(n) for n,v in b['materials'].items()),'original_meshes_removed':sorted(removed),'foliage_z_only_count':len(zonly),'image_relative_path_remaps_only':paths,'original_object_differences':diff,'errors':errors,'scope_checks_pass':not errors}
(out/'Saved_Source_Review.json').write_text(json.dumps(report,ensure_ascii=False,indent=2));print(json.dumps({k:v for k,v in report.items() if k!='original_object_differences'},ensure_ascii=False,indent=2))
