"""Read-only persisted-datablock snapshot. Does not import author scripts or save a blend."""
import bpy, json, hashlib, argparse, sys, math
from pathlib import Path

def sha(p):
    h=hashlib.sha256()
    with open(p,'rb') as f:
        for block in iter(lambda:f.read(1024*1024),b''):h.update(block)
    return h.hexdigest()
def norm(x):
    if x is None or isinstance(x,(str,int,bool)):return x
    if isinstance(x,float):return x if math.isfinite(x) else str(x)
    if isinstance(x,bpy.types.ID):return {'id_type':x.__class__.__name__,'name':x.name_full}
    if hasattr(x,'to_dict'):return {str(k):norm(v) for k,v in x.to_dict().items()}
    if isinstance(x,dict):return {str(k):norm(v) for k,v in x.items()}
    try:return [norm(v) for v in x]
    except TypeError:return str(x)
def h(x):return hashlib.sha256(json.dumps(norm(x),sort_keys=True,ensure_ascii=False,separators=(',',':')).encode()).hexdigest()
def custom(x):return {k:norm(x[k]) for k in x.keys()}
def fields(x,names):
    out={}
    for k in names:
        if hasattr(x,k):
            try:out[k]=norm(getattr(x,k))
            except (AttributeError,ValueError,TypeError):pass
    return out
def rna(x):
    out={}
    for p in x.bl_rna.properties:
        k=p.identifier
        if k=='rna_type' or p.type=='COLLECTION':continue
        if k in {'id_data','original'}:continue
        try:
            v=getattr(x,k)
            if p.type=='POINTER' and v is not None and not isinstance(v,bpy.types.ID):continue
            out[k]=norm(v)
        except (AttributeError,ValueError,TypeError):pass
    return out
def node_tree(nt):
    if nt is None:return None
    nodes={}
    for n in nt.nodes:
        row=rna(n)
        row['inputs']=[{'identifier':s.identifier,'name':s.name,'type':s.type,**fields(s,['default_value'])} for s in n.inputs]
        row['outputs']=[{'identifier':s.identifier,'name':s.name,'type':s.type,**fields(s,['default_value'])} for s in n.outputs]
        if hasattr(n,'color_ramp'):
            cr=n.color_ramp;row['color_ramp']={**rna(cr),'elements':[{'position':e.position,'color':list(e.color)} for e in cr.elements]}
        if hasattr(n,'mapping') and hasattr(n.mapping,'curves'):
            row['mapping']={**rna(n.mapping),'curves':[[fields(p,['location','handle_type']) for p in c.points] for c in n.mapping.curves]}
        nodes[n.name]=row
    return {'nodes':nodes,'links':sorted([[l.from_node.name,l.from_socket.identifier,l.to_node.name,l.to_socket.identifier] for l in nt.links])}
def mesh(m):
    attrs={}
    for a in m.attributes:
        vals=[]
        for d in a.data:vals.append(fields(d,['value','vector','color','byte_color']))
        attrs[a.name]={'type':a.data_type,'domain':a.domain,'values_sha256':h(vals)}
    row={'name':m.name,'custom':custom(m),'vertices_count':len(m.vertices),'vertices_sha256':h([list(v.co) for v in m.vertices]),'edges_sha256':h([[*e.vertices,e.use_seam,e.use_edge_sharp] for e in m.edges]),'faces_sha256':h([[list(p.vertices),p.material_index,p.use_smooth] for p in m.polygons]),'faces_count':len(m.polygons),'uv':{uv.name:h([list(v.uv) for v in uv.data]) for uv in m.uv_layers},'active_uv':m.uv_layers.active.name if m.uv_layers.active else None,'material_names':[a.name if a else None for a in m.materials],'weights_sha256':h([[[g.group,g.weight] for g in v.groups] for v in m.vertices]),'attributes':attrs,'has_custom_normals':m.has_custom_normals}
    if m.has_custom_normals:row['corner_normals_sha256']=h([list(n.vector) for n in m.corner_normals])
    if m.shape_keys:row['shape_keys']={k.name:{'relative_key':k.relative_key.name,'coordinates_sha256':h([list(p.co) for p in k.data]),'value':k.value} for k in m.shape_keys.key_blocks}
    return row
def stored(o):return fields(o,['location','rotation_mode','rotation_euler','rotation_quaternion','rotation_axis_angle','scale','delta_location','delta_rotation_euler','delta_rotation_quaternion','delta_scale','parent','parent_type','parent_bone','matrix_parent_inverse'])
def obj(o):
    row={'type':o.type,'data':o.data.name if o.data else None,'custom':custom(o),'transform':stored(o),'collections':sorted(c.name for c in o.users_collection),'hide_render':o.hide_render,'hide_viewport':o.hide_viewport,'hide_get':o.hide_get(),'display_type':o.display_type,'modifiers':[rna(m) for m in o.modifiers],'constraints':[rna(c) for c in o.constraints],'vertex_groups':[(g.name,g.lock_weight) for g in o.vertex_groups],'material_slots':[{'link':s.link,'material':s.material.name if s.material else None} for s in o.material_slots],'instance_type':o.instance_type,'instance_collection':o.instance_collection.name if o.instance_collection else None}
    if o.type=='ARMATURE':row['pose']={b.name:{**fields(b,['location','rotation_mode','rotation_quaternion','rotation_euler','scale','matrix_basis']),'constraints':[rna(c) for c in b.constraints]} for b in o.pose.bones}
    if o.animation_data:row['action']=o.animation_data.action.name if o.animation_data.action else None;row['drivers']=[{'data_path':d.data_path,'array_index':d.array_index,'expression':d.driver.expression} for d in o.animation_data.drivers]
    return row
ap=argparse.ArgumentParser();ap.add_argument('--source',required=True);ap.add_argument('--output',required=True);a=ap.parse_args(sys.argv[sys.argv.index('--')+1:]);s=sha(a.source)
bpy.ops.wm.open_mainfile(filepath=str(Path(a.source).resolve()));bpy.context.view_layer.update()
report={'source':Path(a.source).name,'source_sha256':s,'script_sha256':sha(__file__),'blender':bpy.app.version_string,'method':'Read-only saved source raw datablocks, actual data linkage, persisted transforms, material nodes, bone rest data and pose. No author code imported. No save operation.','objects':{o.name:obj(o) for o in bpy.data.objects},'meshes':{m.name:mesh(m) for m in bpy.data.meshes},'materials':{m.name:{'custom':custom(m),**fields(m,['use_nodes','diffuse_color','metallic','roughness','surface_render_method','displacement_method','use_backface_culling']),'node_tree':node_tree(m.node_tree)} for m in bpy.data.materials},'images':{im.name:{**fields(im,['size','source','filepath','alpha_mode']),'colorspace':im.colorspace_settings.name,'packed_sha256':hashlib.sha256(bytes(im.packed_file.data)).hexdigest() if im.packed_file else None} for im in bpy.data.images},'armatures':{ar.name:{'bones':{b.name:{'parent':b.parent.name if b.parent else None,**fields(b,['head_local','tail_local','matrix_local','use_deform','envelope_distance','envelope_weight','head_radius','tail_radius','inherit_scale']),'custom':custom(b)} for b in ar.bones}} for ar in bpy.data.armatures},'collections':{c.name:{'objects':sorted(o.name for o in c.objects),'children':sorted(o.name for o in c.children),'hide_render':c.hide_render,'hide_viewport':c.hide_viewport,'instance_offset':list(c.instance_offset),'custom':custom(c)} for c in bpy.data.collections},'curves':{c.name:{'dimensions':c.dimensions,'bevel_depth':c.bevel_depth,'extrude':c.extrude,'splines':[{'type':sp.type,'points':[list(p.co) for p in sp.points],'bezier_points':[fields(p,['co','handle_left','handle_right','handle_left_type','handle_right_type']) for p in sp.bezier_points],'use_cyclic_u':sp.use_cyclic_u} for sp in c.splines]} for c in bpy.data.curves},'node_groups':{g.name:node_tree(g) for g in bpy.data.node_groups},'cameras':{c.name:rna(c) for c in bpy.data.cameras},'lights':{l.name:rna(l) for l in bpy.data.lights}}
report['mesh_users']={m.name:sorted(o.name for o in bpy.data.objects if o.type=='MESH' and o.data==m) for m in bpy.data.meshes}
report['source_unchanged']=s==sha(a.source)
Path(a.output).parent.mkdir(parents=True,exist_ok=True);Path(a.output).write_text(json.dumps(report,ensure_ascii=False,indent=2))
print('SNAPSHOT_DONE',a.output,'objects',len(report['objects']),'meshes',len(report['meshes']),'materials',len(report['materials']),'source_unchanged',report['source_unchanged'],flush=True)
