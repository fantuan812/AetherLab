import bpy,json,hashlib,struct,sys
out=sys.argv[sys.argv.index('--')+1]
def digest(vals):
 h=hashlib.sha256()
 for v in vals:
  for a in v:h.update(struct.pack('<d',float(a)))
 return h.hexdigest()
r=bpy.data.objects['SCALE_REFERENCE_TRANSLATION_ONLY'];res={'file':bpy.data.filepath,'root_translation':list(r.location),'root_scale':list(r.scale),'root_rotation':list(r.rotation_euler),'objects':{}}
for o in r.children_recursive:
 d={'type':o.type,'matrix_local':[list(row) for row in o.matrix_local]}
 if o.type=='MESH':
  m=o.data;d.update({'vertices':len(m.vertices),'polygons':len(m.polygons),'coordinates':digest([v.co for v in m.vertices]),'polygon_indices_sha256':hashlib.sha256(json.dumps([list(p.vertices) for p in m.polygons],separators=(',',':')).encode()).hexdigest(),'uv':{u.name:digest([p.uv for p in u.data]) for u in m.uv_layers},'vertex_group_weights':digest([[v.index,g.group,g.weight] for v in m.vertices for g in v.groups]),'materials':[m.name for m in m.materials],'modifiers':[(x.name,x.type) for x in o.modifiers]})
 if o.type=='ARMATURE':d['bones']={b.name:{'head':list(b.head_local),'tail':list(b.tail_local),'parent':b.parent.name if b.parent else None} for b in o.data.bones}
 res['objects'][o.name]=d
json.dump(res,open(out,'w'),ensure_ascii=False,indent=2)
