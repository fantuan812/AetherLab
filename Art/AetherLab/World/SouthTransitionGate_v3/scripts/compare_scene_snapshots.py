"""Compare independent persisted snapshots; explicitly constrain allowed changes."""
import json,argparse,hashlib
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--before',required=True);p.add_argument('--after',required=True);p.add_argument('--changes',required=True);p.add_argument('--output',required=True);a=p.parse_args();b=json.load(open(a.before));c=json.load(open(a.after));ch=json.load(open(a.changes))
def clean(x):
 if isinstance(x,dict):return {k:clean(v) for k,v in x.items() if k!='session_uid'}
 if isinstance(x,list):return [clean(v) for v in x]
 return x
b=clean(b);c=clean(c);errors=[];moves={v['object']:v for v in ch['foliage_contacts']};expected={'SCN03_Gate_Pier-4':{'data','material_slots'},'SCN03_Gate_Pier4':{'data','material_slots'},'SCN03_Gate_Lintel':{'data','material_slots'},'SCN03_Gate_Roof':{'data'},'SCN03_Registration_Canopy':{'hide_render','hide_viewport'},'WORLD_Terrain_Continuous_800x800m':{'custom','material_slots'}};changed=[]
for n,o in b['objects'].items():
 if n not in c['objects']:errors.append('Missing object '+n);continue
 z=c['objects'][n];keys={k for k in o if o[k]!=z[k]}
 if not keys:continue
 changed.append({'object':n,'fields':sorted(keys)})
 if n in moves:
  if keys!={'transform'}:errors.append('Extra contact mutation '+n)
  bo,co=o['transform'].copy(),z['transform'].copy();bl=bo.pop('location');cl=co.pop('location')
  if bo!=co or bl[:2]!=cl[:2]:errors.append('Nonvertical contact mutation '+n)
 elif keys!=expected.get(n,set()):errors.append('Unexpected object mutation '+n+' '+str(keys))
allowed_removed={'SCN03_Gate_Pier-4','SCN03_Gate_Pier4','SCN03_Gate_Lintel','SCN03_Gate_Roof'}
for n,me in b['meshes'].items():
 if n in allowed_removed:continue
 if n not in c['meshes']:errors.append('Missing retained mesh '+n)
 elif n!='WORLD_Terrain_Continuous_800x800m' and me!=c['meshes'][n]:errors.append('Changed retained mesh '+n)
for group in ['materials','armatures','cameras','lights','curves','node_groups']:
 for n,o in b[group].items():
  if n not in c[group] or o!=c[group][n]:errors.append('Changed original '+group+' '+n)
image_path_remaps=[]
for n,o in b['images'].items():
 z=c['images'].get(n,{});bo=o.copy();co=z.copy();bp=bo.pop('filepath',None);cp=co.pop('filepath',None)
 if bo!=co or not bo.get('packed_sha256'):errors.append('Changed original image payload '+n)
 if bp!=cp:image_path_remaps.append({'image':n,'before_path':bp,'after_path':cp,'packed_bytes_sha256':bo.get('packed_sha256')})
route_objects=[n for n,o in b['objects'].items() if n.startswith('ROUTE_')];route_ok=all(b['objects'][n]==c['objects'][n] and b['meshes'][b['objects'][n]['data']]==c['meshes'][c['objects'][n]['data']] for n in route_objects)
report={'before_sha256':b['source_sha256'],'after_sha256':c['source_sha256'],'before_snapshot_sha256':hashlib.sha256(Path(a.before).read_bytes()).hexdigest(),'after_snapshot_sha256':hashlib.sha256(Path(a.after).read_bytes()).hexdigest(),'ignored_runtime_field':'session_uid only','packed_image_path_remaps':image_path_remaps,'route_objects':len(route_objects),'route_objects_and_meshes_exact':route_ok,'original_materials_exact':all(b['materials'][n]==c['materials'][n] for n in b['materials']),'original_armatures_exact':b['armatures']==c['armatures'],'original_meshes_removed':sorted(set(b['meshes'])-set(c['meshes'])),'new_objects':len(set(c['objects'])-set(b['objects'])),'changed_original_objects':changed,'foliage_vertical_only_count':len(moves),'errors':errors,'passed':not errors and route_ok,'limits':['Four SCN03 core mesh IDs intentionally replaced; their old unused mesh IDs disappear on save','Terrain topology and local heights intentionally changed','No UE compiled or runtime tested']};Path(a.output).write_text(json.dumps(report,indent=2));print(json.dumps({k:v for k,v in report.items() if k!='changed_original_objects'},indent=2));assert report['passed']
