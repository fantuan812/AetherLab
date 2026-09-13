import bpy,json
for name in ('Oathwanderer','BellKnight_Auren'):
    ob=bpy.data.objects['SK_'+name];mesh=ob.data
    adjacent=[[] for v in mesh.vertices]
    for e in mesh.edges:a,b=e.vertices;adjacent[a].append(b);adjacent[b].append(a)
    visited=set();rows=[]
    for v in mesh.vertices:
        if v.index in visited:continue
        stack=[v.index];ids=[];visited.add(v.index)
        while stack:
            a=stack.pop();ids.append(a)
            for b in adjacent[a]:
                if b not in visited:visited.add(b);stack.append(b)
        groups={ob.vertex_groups[g.group].name for i in ids for g in mesh.vertices[i].groups}
        if not groups.intersection({'hand_l','hand_r'}):continue
        rows.append({'ids_start':ids[0],'vertices':len(ids),'center':[sum(mesh.vertices[i].co[j] for i in ids)/len(ids) for j in range(3)],'groups':sorted(groups)})
    print(name,json.dumps(rows))
