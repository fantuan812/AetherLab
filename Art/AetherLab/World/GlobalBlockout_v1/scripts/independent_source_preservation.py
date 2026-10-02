"""Cross-file read-only source fidelity checks; no saved file mutation."""
import bpy,json,hashlib,sys,os,struct,argparse
from pathlib import Path
from mathutils import Vector
import math
BASE=Path(__file__).resolve().parent.parent
DEFAULT_ROOT=BASE if (BASE/'source').exists() else BASE/'deliverables'
ap=argparse.ArgumentParser(description=__doc__)
ap.add_argument('--source',default=str(DEFAULT_ROOT/'source/AetherLab_Global_World_Blockout_v1.blend'))
ap.add_argument('--manifest',default=str(DEFAULT_ROOT/'docs/World_Manifest.json'))
ap.add_argument('--kit',required=True,help='Frozen SharedCore_v3 source .blend')
ap.add_argument('--legacy',required=True,help='Read-only SCN01 legacy source .blend')
ap.add_argument('--output',default=str(BASE/'review/independent_source_preservation.json'))
args=ap.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
SRC,KIT,LEGACY,OUT=args.source,args.kit,args.legacy,args.output
os.makedirs(os.path.dirname(os.path.abspath(OUT)),exist_ok=True)
sha=lambda p:hashlib.sha256(open(p,'rb').read()).hexdigest()
def val(v):
 try:return [float(x) for x in v]
 except:return str(v)
def hashjson(v):return hashlib.sha256(json.dumps(v,sort_keys=True,ensure_ascii=False,separators=(',',':')).encode()).hexdigest()
def mesh(o):
 m=o.data
 return {'vertices':hashjson([list(v.co) for v in m.vertices]),'faces':hashjson([[list(p.vertices),p.material_index] for p in m.polygons]),'uv':{uv.name:hashjson([list(x.uv) for x in uv.data]) for uv in m.uv_layers},'weights':hashjson([[[g.group,g.weight] for g in v.groups] for v in m.vertices]),'vertex_groups':[g.name for g in o.vertex_groups],'material_names':[a.name if a else None for a in m.materials]}
def stored_transform(o):
 # A hidden collection may leave matrix_world unevaluated on reopen. Compare persisted transform fields.
 return {'location':list(o.location),'rotation_mode':o.rotation_mode,'rotation_euler':list(o.rotation_euler),'rotation_quaternion':list(o.rotation_quaternion),'scale':list(o.scale),'parent':o.parent.name if o.parent else None,'matrix_parent_inverse':[list(r) for r in o.matrix_parent_inverse]}
def mats():
 out={}
 for m in bpy.data.materials:
  rr={'diffuse':list(m.diffuse_color),'nodes':[],'links':[]}
  if m.use_nodes:
   for n in m.node_tree.nodes:
    d={'name':n.name,'type':n.type,'inputs':{i.name:val(i.default_value) for i in n.inputs if hasattr(i,'default_value')}}
    if n.type=='UVMAP':d['uv_map']=n.uv_map
    if n.type=='TEX_IMAGE':
     d['image_packed_sha256']=hashlib.sha256(bytes(n.image.packed_file.data)).hexdigest() if n.image and n.image.packed_file else None
     d['image_size']=list(n.image.size) if n.image else None
     d['image_colorspace']=n.image.colorspace_settings.name if n.image else None
    rr['nodes'].append(d)
   rr['links']=[(l.from_node.name,l.from_socket.name,l.to_node.name,l.to_socket.name) for l in m.node_tree.links]
  out[m.name]=hashjson(rr)
 return out
bpy.ops.wm.open_mainfile(filepath=SRC);world_sha=sha(SRC)
manifest=json.load(open(args.manifest));world_legacy={}
for rec in manifest['reused_sources']:
 o=bpy.data.objects[rec['master']];m=o.data
 world_legacy[rec['master']]={'vertices':[list(v.co) for v in m.vertices],'faces':[list(p.vertices) for p in m.polygons],'face_materials':[m.materials[p.material_index].name for p in m.polygons],'uv':{uv.name:[list(t.uv) for t in uv.data] for uv in m.uv_layers},'matrix_world':[list(r) for r in o.matrix_world]}
archive=bpy.data.collections.get('ARCHIVE_Original_SCN01_Shelter_Components')
world_archive={o.name:{'mesh':mesh(o),'stored_transform':stored_transform(o)} for o in archive.objects if o.type=='MESH'} if archive else {}
world_master={o.name:mesh(o) for c in bpy.data.collections if c.name in ['01_MASTER_MODULES__4m_Candidate','05_TRANSITION_MASTERS__CANDIDATES'] for o in c.objects if o.type=='MESH'}
charcoll=bpy.data.collections['05_EXISTING_CHARACTER__UNSCALED_65_BONES']
world_char={o.name:mesh(o) for o in charcoll.objects if o.type=='MESH'}
def bones(o):return hashjson([[b.name,b.parent.name if b.parent else None,list(b.head_local),list(b.tail_local),[list(r) for r in b.matrix_local]] for b in o.data.bones])
world_bones={o.name:bones(o) for o in charcoll.objects if o.type=='ARMATURE'}
world_mats=mats();world_objnames=set(bpy.data.objects.keys());world_fixture_props=[o.name for o in bpy.data.objects if o.get('fixture')];char_local={o.name:[list(r) for r in o.matrix_basis] for o in charcoll.objects if o.name!='SCALE_REFERENCE_TRANSLATION_ONLY'}
report={'reviewer_script_sha256':sha(__file__),'source_sha256':world_sha,'manifest_sha256':sha(args.manifest),'manifest_matches_source':manifest.get('source_sha256')==world_sha,'kit_sha256':sha(KIT),'legacy_sha256':sha(LEGACY),'method':'Reopen world, frozen v3 kit, and legacy source separately; compare raw mesh vertices/faces/weights/UV signatures, bone rest matrices, material node/socket values. No save.'}
bpy.ops.wm.open_mainfile(filepath=KIT)
source_master={o.name:mesh(o) for c in bpy.data.collections if c.name in ['01_MASTER_MODULES__4m_Candidate','05_TRANSITION_MASTERS__CANDIDATES'] for o in c.objects if o.type=='MESH'}
kit_materials=mats();kit_fixture=[o.name for o in bpy.data.objects if o.get('fixture')];fixture_collections=[c.name for c in bpy.data.collections if 'FIXTURE' in c.name];fixture_names=set(o.name for c in bpy.data.collections if 'FIXTURE' in c.name for o in c.all_objects)
report['kit_masters']={'world_count':len(world_master),'source_count':len(source_master),'changed':{n:[k for k,v in a.items() if source_master.get(n,{}).get(k)!=v] for n,a in world_master.items() if a!=source_master.get(n)}}
report['fixture_exclusion']={'source_fixture_collections':fixture_collections,'source_fixture_object_count':len(fixture_names),'source_fixture_tagged_count':len(kit_fixture),'world_fixture_tagged_objects':world_fixture_props,'source_fixture_names_present_in_world':sorted(fixture_names&world_objnames)}
source_char={o.name:mesh(o) for c in bpy.data.collections if 'CHARACTER' in c.name for o in c.objects if o.type=='MESH'}
source_bones={o.name:bones(o) for c in bpy.data.collections if 'CHARACTER' in c.name for o in c.objects if o.type=='ARMATURE'}
report['character']={'mesh_count':len(world_char),'mesh_changes':{n:[k for k,v in a.items() if source_char.get(n,{}).get(k)!=v] for n,a in world_char.items() if a!=source_char.get(n)},'bone_hashes_equal':world_bones==source_bones,'local_transform_changes':[n for n,m in char_local.items() if n not in bpy.data.objects or [list(r) for r in bpy.data.objects[n].matrix_basis]!=m]}
bpy.ops.wm.open_mainfile(filepath=LEGACY);legacy_materials=mats()
source_mats={**legacy_materials,**kit_materials}
shared=set(world_mats)&set(source_mats);report['materials']={'source_named_material_count':len(shared),'changed_material_node_signatures':[n for n in shared if world_mats[n]!=source_mats[n]],'world_only_names':sorted(set(world_mats)-shared),'kit_material_count':len(kit_materials),'legacy_material_count':len(legacy_materials)}

# Independent reconstruction from the original evaluated source objects named in the manifest.
# Only the documented anchor translation is applied. No author build code is imported.
bpy.context.view_layer.update();dg=bpy.context.evaluated_depsgraph_get();legacy_rows=[]
for rec in manifest['reused_sources']:
 name=rec['master'];target=world_legacy[name];anchor=Vector(rec['source_anchor_m']);verts=[];faces=[];face_mats=[];uvs=[];missing=[]
 for objname in rec['source_objects']:
  obj=bpy.data.objects.get(objname)
  if obj is None:missing.append(objname);continue
  ev=obj.evaluated_get(dg);me=ev.to_mesh(preserve_all_data_layers=True,depsgraph=dg);offset=len(verts)
  verts += [list(obj.matrix_world@v.co-anchor) for v in me.vertices]
  layer=me.uv_layers.active
  for poly in me.polygons:
   faces.append([offset+i for i in poly.vertices]);mat=me.materials[poly.material_index] if len(me.materials)>poly.material_index else None;face_mats.append(mat.name if mat else None)
   uvs += [list(layer.data[i].uv) if layer else [0.0,0.0] for i in poly.loop_indices]
  ev.to_mesh_clear()
 nv=len(verts)==len(target['vertices']);nf=faces==target['faces'];nm=face_mats==target['face_materials']
 delta=max((math.sqrt(sum((x-y)**2 for x,y in zip(a,b))) for a,b in zip(verts,target['vertices'])),default=0) if nv else None
 selected_uv=target['uv'].get('SurfaceUV',[]);nuv=len(uvs)==len(selected_uv)
 uvdelta=max((abs(x-y) for a,b in zip(uvs,selected_uv) for x,y in zip(a,b)),default=0) if nuv else None
 extra_uv={n:{'matches_active_source_uv':len(arr)==len(uvs) and max((abs(x-y) for a,b in zip(uvs,arr) for x,y in zip(a,b)),default=0)<=1e-6} for n,arr in target['uv'].items() if n!='SurfaceUV'}
 good=not missing and nv and delta<=1e-5 and nf and nm and nuv and uvdelta<=1e-6 and all(v['matches_active_source_uv'] for v in extra_uv.values())
 legacy_rows.append({'master':name,'source_objects':rec['source_objects'],'source_anchor_m':rec['source_anchor_m'],'source_object_count':len(rec['source_objects']),'missing_objects':missing,'evaluated_source_vertices':len(verts),'stored_vertices':len(target['vertices']),'max_vertex_distance_m':delta,'faces_exact':nf,'face_material_names_exact':nm,'source_active_uv_stream_max_delta':uvdelta,'additional_uv_layers':extra_uv,'pass':good})
 print('LEGACY_MASTER',name,good,'max_vertex_delta',delta,'faces',nf,'UV',uvdelta,flush=True)
report['legacy_merged_masters']={'master_count':len(legacy_rows),'vertex_tolerance_m':1e-5,'uv_tolerance':1e-6,'pass':all(x['pass'] for x in legacy_rows),'rows':legacy_rows,'scope':'Saved normalized mesh compared against independently evaluated named source objects with anchor translation. Source active UV stream and duplicated AtlasUV are checked; unreferenced original UV layers, smooth flags/custom normals, and all legacy objects outside the named set are not covered.'}
archive_rows=[]
for name,d in world_archive.items():
 obj=bpy.data.objects.get(name);current=None if obj is None or obj.type!='MESH' else {'mesh':mesh(obj),'stored_transform':stored_transform(obj)}
 archive_rows.append({'object':name,'pass':d==current,'differences':list(d.keys()) if current is None else [k for k in d if d[k]!=current[k]]})
report['original_shelter_archive']={'mesh_object_count':len(archive_rows),'pass':all(x['pass'] for x in archive_rows),'changed':[r for r in archive_rows if not r['pass']],'scope':'Raw mesh/UV/weights/material-slot hashes plus persisted local transform fields and parent binding. Hidden collection matrix_world caches are intentionally not used. This is not rendered pixel equivalence.'}

report['source_unchanged_during_review']=sha(SRC)==world_sha
json.dump(report,open(OUT,'w'),ensure_ascii=False,indent=2)
print(json.dumps(report,ensure_ascii=False,indent=2))
