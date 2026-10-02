"""Independent structural comparison of two reopened Blender snapshots."""
import json,argparse,hashlib,re
from pathlib import Path
ap=argparse.ArgumentParser();ap.add_argument('--baseline',required=True);ap.add_argument('--current',required=True);ap.add_argument('--output',required=True);a=ap.parse_args()
b=json.load(open(a.baseline));c=json.load(open(a.current))
def changed_fields(x,y):return [k for k in sorted(set(x)|set(y)) if x.get(k)!=y.get(k)]
def table(k):
    x,y=b[k],c[k]
    if k in {'cameras','lights'}:
        # Blender assigns session_uid at load time; it is not persisted optical or light state.
        x={n:{f:v for f,v in row.items() if f!='session_uid'} for n,row in x.items()}
        y={n:{f:v for f,v in row.items() if f!='session_uid'} for n,row in y.items()}
    return {'baseline_count':len(x),'current_count':len(y),'removed':sorted(set(x)-set(y)),'added':sorted(set(y)-set(x)),'changed':{n:changed_fields(x[n],y[n]) for n in sorted(set(x)&set(y)) if x[n]!=y[n]}}
r={'baseline_sha256':b['source_sha256'],'current_sha256':c['source_sha256'],'baseline_source':b['source'],'current_source':c['source'],'method':'Compare raw saved datablock fingerprints extracted independently from reopened sources. Exact comparison reports changes rather than accepting author metadata. No author build import or file mutation.','source_unchanged_during_snapshots':b['source_unchanged'] and c['source_unchanged'],'all_categories':{k:table(k) for k in ['objects','meshes','materials','images','armatures','collections','curves','node_groups','cameras','lights']}}

def check_objects(names):
    names=sorted(names);out={'count':len(names),'removed':[],'object_changes':{},'mesh_changes':{},'armature_changes':{}}
    for n in names:
        if n not in c['objects']:out['removed'].append(n);continue
        bo,co=b['objects'][n],c['objects'][n]
        if bo!=co:out['object_changes'][n]=changed_fields(bo,co)
        if bo['type']=='MESH' and bo['data'] in b['meshes']:
            bm,cm=b['meshes'][bo['data']],c['meshes'].get(co['data'])
            if bm!=cm:out['mesh_changes'][n]=changed_fields(bm,cm or {})
        if bo['type']=='ARMATURE' and b['armatures'][bo['data']]!=c['armatures'].get(co['data']):out['armature_changes'][n]=True
    out['pass']=not any(out[k] for k in ['removed','object_changes','mesh_changes','armature_changes']);return out

scene=[n for n,o in b['objects'].items() if o['custom'].get('scene_id')]
route=[n for n,o in b['objects'].items() if o['custom'].get('role')=='walkable_route']
masters=[n for n,o in b['objects'].items() if any('MASTER' in x or 'ARCHIVE' in x for x in o['collections'])]
characters=[n for n,o in b['objects'].items() if any('CHARACTER' in x for x in o['collections'])]
anchors=[n for n,o in b['objects'].items() if o['type']=='EMPTY' or o['custom'].get('prop_id') or o['custom'].get('PROP_ID')]
r['protected_scene_anchors']=check_objects(scene);r['protected_route_surfaces']=check_objects(route);r['protected_gameplay_anchors']=check_objects(anchors);r['protected_masters_archive']=check_objects(masters);r['protected_character']=check_objects(characters)
r['baseline_materials']={'count':len(b['materials']),'removed':r['all_categories']['materials']['removed'],'changed':r['all_categories']['materials']['changed']}
r['baseline_images']={'count':len(b['images']),'removed':r['all_categories']['images']['removed'],'changed':r['all_categories']['images']['changed']}
old_instances=[n for n,o in b['objects'].items() if o['custom'].get('master')]
new_names=sorted(set(c['objects'])-set(b['objects']))
new_meshes=[n for n in new_names if c['objects'][n]['type']=='MESH']

def sharing(names):
    bad=[];missing=[];groups={}
    for n in names:
        o=c['objects'].get(n)
        if not o:missing.append(n);continue
        mn=o['custom'].get('master');master=c['objects'].get(mn) if mn else None
        if mn and (master is None or master['data']!=o['data']):bad.append(n)
        groups.setdefault(o['data'],[]).append(n)
    return {'count':len(names),'missing_objects':missing,'declared_master_link_mismatches':bad,'unique_datablocks':len(groups),'data_groups':{k:{'reviewed_object_count':len(v),'all_saved_user_count':len(c['mesh_users'].get(k,[])),'objects':v} for k,v in sorted(groups.items())}}
r['old_declared_instance_sharing']=sharing(old_instances);r['all_new_mesh_sharing']=sharing(new_meshes)
veg=[n for n in new_meshes if re.search(r'tree|shrub|bush|grass|veget|fern|foliage|canopy|reed',n+' '+str(c['objects'][n]['custom']),re.I)]
r['new_vegetation_name_or_role_candidates']=sharing(veg)
Path(a.output).parent.mkdir(parents=True,exist_ok=True);Path(a.output).write_text(json.dumps(r,ensure_ascii=False,indent=2))
print(json.dumps({'baseline':r['baseline_sha256'],'current':r['current_sha256'],'categories':{k:{'added':len(v['added']),'removed':len(v['removed']),'changed':len(v['changed'])} for k,v in r['all_categories'].items()},'checks':{k:r[k]['pass'] for k in ['protected_scene_anchors','protected_route_surfaces','protected_gameplay_anchors','protected_masters_archive','protected_character']}},ensure_ascii=False,indent=2))
