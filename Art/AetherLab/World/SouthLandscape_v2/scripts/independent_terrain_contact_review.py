"""Independent read-only before/after geometric changes and actual terrain-contact probes."""
import bpy,json,argparse,sys,hashlib,collections,math
from pathlib import Path
from mathutils import Vector
from mathutils.bvhtree import BVHTree
ap=argparse.ArgumentParser();ap.add_argument('--baseline',required=True);ap.add_argument('--current',required=True);ap.add_argument('--output',required=True);a=ap.parse_args(sys.argv[sys.argv.index('--')+1:])
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
bs,cs=sha(a.baseline),sha(a.current)
bpy.ops.wm.open_mainfile(filepath=str(Path(a.baseline).resolve()));tm=bpy.data.objects['WORLD_Terrain_Continuous_800x800m'].data
beforeverts=[tuple(v.co) for v in tm.vertices];beforefaces=[(tuple(p.vertices),p.material_index,p.use_smooth) for p in tm.polygons];beforeobjs={o.name:{'location':tuple(o.location),'matrix':tuple(tuple(x) for x in o.matrix_world),'data':o.data.name if o.data else None,'collections':[c.name for c in o.users_collection]} for o in bpy.data.objects}
bpy.ops.wm.open_mainfile(filepath=str(Path(a.current).resolve()));bpy.context.view_layer.update();terrain=bpy.data.objects['WORLD_Terrain_Continuous_800x800m'];tm=terrain.data
changes=[];badxy=[]
for i,(old,v) in enumerate(zip(beforeverts,tm.vertices)):
    new=tuple(v.co)
    if old!=new:changes.append({'index':i,'before':old,'after':new,'delta_z':new[2]-old[2]})
    if old[:2]!=new[:2]:badxy.append(i)
faceschanged=[];topology=[];smooth=[];matidx=[]
for i,(old,p) in enumerate(zip(beforefaces,tm.polygons)):
    if old[0]!=tuple(p.vertices):topology.append(i)
    if old[1]!=p.material_index:matidx.append(i)
    if old[2]!=p.use_smooth:smooth.append({'index':i,'was_smooth':old[2],'is_smooth':p.use_smooth,'xy_bounds':[[min(tm.vertices[v].co[k] for v in p.vertices),max(tm.vertices[v].co[k] for v in p.vertices)] for k in (0,1)]})
verts=[terrain.matrix_world@v.co for v in tm.vertices];bvh=BVHTree.FromPolygons(verts,[tuple(p.vertices) for p in tm.polygons])
newobjs=[o for o in bpy.data.objects if o.name not in beforeobjs];oldmoved=[o for o in bpy.data.objects if o.name in beforeobjs and tuple(o.location)!=beforeobjs[o.name]['location']]
def contact(o):
    worldpts=[o.matrix_world@v.co for v in o.data.vertices];bottom=min(v.z for v in worldpts);origin=o.matrix_world.translation
    hit=bvh.ray_cast(Vector((origin.x,origin.y,100)),Vector((0,0,-1)),200)[0]
    return {'object':o.name,'world_origin':list(origin),'lowest_world_mesh_z':bottom,'terrain_z_at_origin':hit.z if hit else None,'bottom_vs_origin_ground_m':bottom-hit.z if hit else None}
newcontacts=[contact(o) for o in newobjs if o.type=='MESH'];oldcontacts=[contact(o) for o in oldmoved if o.type=='MESH'];actualgroups=collections.defaultdict(list)
for o in newobjs:actualgroups[o.data.name if o.data else None].append(o.name)
active=set()
def visit(c,hidden=False):
    hidden=hidden or c.hide_render
    if not hidden:active.update(o.name for o in c.objects if not o.hide_render)
    for cc in c.children:visit(cc,hidden)
visit(bpy.context.scene.collection)
oldmoves=[{'object':o.name,'before':beforeobjs[o.name]['location'],'after':list(o.location),'xy_unchanged':tuple(o.location)[:2]==beforeobjs[o.name]['location'][:2],'old_collections':beforeobjs[o.name]['collections']} for o in oldmoved]
report={'baseline_sha256':bs,'current_sha256':cs,'terrain':{'before_vertex_count':len(beforeverts),'after_vertex_count':len(tm.vertices),'before_face_count':len(beforefaces),'after_face_count':len(tm.polygons),'actual_changed_vertex_count':len(changes),'changed_xy_indices':badxy,'topology_changed_face_indices':topology,'changed_material_index_faces':matidx,'changed_smooth_face_count':len(smooth),'smooth_changes':smooth,'changes':changes,'max_abs_z_change_m':max(abs(x['delta_z']) for x in changes),'changed_xy_bounds_m':[[min(x['before'][k] for x in changes),max(x['before'][k] for x in changes)] for k in (0,1)]},'old_moved_objects':oldmoves,'new_objects':{'count':len(newobjs),'actual_data_group_count':len(actualgroups),'data_group_counts':{k:len(v) for k,v in actualgroups.items()},'master_link_mismatches':[o.name for o in newobjs if not o.get('master') or not bpy.data.objects.get(o['master']) or o.data!=bpy.data.objects[o['master']].data],'not_active_render_geometry':[o.name for o in newobjs if o.name not in active or o.type!='MESH'],'modifiers_present':[o.name for o in newobjs if o.modifiers],'nonunit_world_scales':[o.name for o in newobjs if any(abs(s-1)>1e-5 for s in o.matrix_world.to_scale())]},'new_contacts':newcontacts,'old_reprojected_contacts':oldcontacts,'contact_scope':'Bottom mesh Z compared with saved actual terrain BVH at object origin; does not prove every broad rock edge follows sloped ground.','source_files_unchanged':bs==sha(a.baseline) and cs==sha(a.current)}
Path(a.output).write_text(json.dumps(report,ensure_ascii=False,indent=2));print('DONE',a.output,'terrain_changed',len(changes),'smooth_changed',len(smooth),'new_objects',len(newobjs),'old_moved',len(oldmoves),'new_max_contact_abs',max(abs(x['bottom_vs_origin_ground_m']) for x in newcontacts),'old_max_contact_abs',max(abs(x['bottom_vs_origin_ground_m']) for x in oldcontacts),flush=True)
