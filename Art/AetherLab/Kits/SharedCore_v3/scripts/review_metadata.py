"""Read-only schema audit: no conflicting inherited interface properties."""
import bpy,json,os,hashlib,sys
masters=list(bpy.data.collections['05_TRANSITION_MASTERS__CANDIDATES'].objects)
yard=list(bpy.data.collections['06_CONNECTED_TRANSITION_FIXTURE__NOT_WORLD'].objects)
allowed={'forward_up','socket_semantics','asset_id','role','derived_from','stage','local_sockets_json','allowed_instance_scale'}
rows=[]
for master in masters:
 items=[master]+[o for o in yard if o.type=='MESH' and o.data is master.data]
 for o in items:
  expected=allowed if o==master else allowed|{'master','fixture'}
  keys=set(o.keys());extra=sorted(keys-expected);missing=sorted(expected-keys)
  mismatches=[]
  if o!=master:mismatches=[k for k in allowed if o.get(k)!=master.get(k)]
  meshprops=list(o.data.keys())
  try:socket_valid=isinstance(json.loads(o['local_sockets_json']),dict)
  except:socket_valid=False
  row={'object':o.name,'kind':'master' if o==master else 'linked_instance','master':master.name,'property_keys':sorted(keys),'unexpected_keys':extra,'missing_keys':missing,'master_value_mismatches':mismatches,'mesh_property_keys':meshprops,'forward_up':o.get('forward_up'),'socket_semantics':o.get('socket_semantics'),'pass':not extra and not missing and not mismatches and not meshprops and socket_valid and o.get('forward_up')=='+Y/+Z' and o.get('allowed_instance_scale')=='1,1,1'}
  rows.append(row)
r={'source_file':os.path.basename(bpy.data.filepath),'source_sha256':hashlib.sha256(open(bpy.data.filepath,'rb').read()).hexdigest(),'expected_master_properties':sorted(allowed),'reviewed_objects':len(rows),'master_count':len(masters),'instance_count':len(rows)-len(masters),'objects':rows,'pass':all(x['pass'] for x in rows),'interpretation':'Exact reviewed schema excludes inherited grid_m, connectors_json, ROAD, EARTH and other legacy interface keys. This is metadata consistency, separate from the mesh/socket geometry audit.'}
json.dump(r,open(os.path.dirname(__file__)+'/Independent_Metadata_Review.json','w'),ensure_ascii=False,indent=2)
print('METADATA',r['pass'],r['master_count'],r['instance_count'])
if not r['pass']:sys.stdout.flush();os._exit(1)
