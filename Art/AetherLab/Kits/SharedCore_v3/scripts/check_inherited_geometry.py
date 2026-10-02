"""Fingerprint inherited v2 geometry, UV, materials, transforms after dependency graph update.
Run separately on v2 and final v3. Does not compare declared expected coordinates.
"""
import bpy,hashlib,json,argparse,sys,os
p=argparse.ArgumentParser();p.add_argument('--output',required=True);p.add_argument('--reference');a=p.parse_args(sys.argv[sys.argv.index('--')+1:]);
for cn in ['01_MASTER_MODULES__4m_Candidate','02_CONNECTION_FIXTURES__NOT_WORLD']:bpy.data.collections[cn].hide_viewport=False
bpy.context.view_layer.update()
rows=[]
for cn in ['01_MASTER_MODULES__4m_Candidate','02_CONNECTION_FIXTURES__NOT_WORLD']:
 for o in sorted(bpy.data.collections[cn].objects,key=lambda x:x.name):
  if o.type!='MESH':continue
  me=o.data
  rows.append({'object':o.name,'matrix':list(v for row in o.matrix_world for v in row),'mesh':me.name,'vertices':[list(v.co) for v in me.vertices],'faces':[list(f.vertices) for f in me.polygons],'uv':{u.name:[list(d.uv) for d in u.data] for u in me.uv_layers},'materials':[m.name for m in me.materials],'modifiers':[(m.name,m.type,m.show_render,m.show_viewport) for m in o.modifiers]})
hash=hashlib.sha256(json.dumps(rows,sort_keys=True).encode()).hexdigest();rep={'source':os.path.basename(bpy.data.filepath),'source_sha256':hashlib.sha256(open(bpy.data.filepath,'rb').read()).hexdigest(),'inherited_mesh_objects':len(rows),'geometry_uv_material_matrix_fingerprint':hash,'scope':'v2 22 master + 678 fixture mesh objects; excludes visibility changes and translated character reference'}
rep['object_field_hashes']={r['object']:{k:hashlib.sha256(json.dumps(v,sort_keys=True).encode()).hexdigest() for k,v in r.items()} for r in rows}
if a.reference:
 prev=json.load(open(a.reference));rep['reference_source_sha256']=prev['source_sha256'];rep['reference_fingerprint']=prev['geometry_uv_material_matrix_fingerprint'];rep['passed']=hash==prev['geometry_uv_material_matrix_fingerprint']
if a.reference:rep['differences']={name:[k for k,h in fields.items() if prev.get('object_field_hashes',{}).get(name,{}).get(k)!=h] for name,fields in rep['object_field_hashes'].items() if fields!=prev.get('object_field_hashes',{}).get(name)}
if a.reference:rep.pop('object_field_hashes',None)
json.dump(rep,open(a.output,'w'),indent=2);print('INHERITED',json.dumps({k:v for k,v in rep.items() if k!='object_field_hashes'}))
if a.reference and not rep['passed']:raise SystemExit(1)
