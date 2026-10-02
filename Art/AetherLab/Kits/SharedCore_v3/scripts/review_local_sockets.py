import bpy,json,os
from mathutils import Vector
from mathutils.bvhtree import BVHTree
out=[]
for o in bpy.data.collections['05_TRANSITION_MASTERS__CANDIDATES'].objects:
 if o.type!='MESH':continue
 m=o.data;m.calc_loop_triangles();vs=[v.co.copy() for v in m.vertices];lo=Vector([min(v[k] for v in vs) for k in range(3)]);hi=Vector([max(v[k] for v in vs) for k in range(3)]);b=BVHTree.FromPolygons(vs,[tuple(t.vertices) for t in m.loop_triangles],all_triangles=True)
 sockets=json.loads(o['local_sockets_json']);r={'master':o.name,'role':o['role'],'local_bounds_m':[list(lo),list(hi)],'sockets':{}}
 for key,xyz in sockets.items():
  p=Vector(xyz);n=b.find_nearest(p);r['sockets'][key]={'coordinates_m':xyz,'closest_mesh_point_m':list(n[0]),'mesh_distance_m':n[3]}
 if o['role']=='stair':
  i=Vector(sockets['IN']);end=Vector(sockets['OUT']);t=b.ray_cast((i.x,i.y+.0001,hi.z+1),(0,0,-1));r['interpretation']='IN is lower-road reference on the first riser plane, not the first tread. OUT is the upper exit/tread edge.';r['IN_to_first_tread_rise_m']=t[0].z-i.z;r['pass']=abs(i.y-lo.y)<.001 and abs(end.y-hi.y)<.001 and r['sockets']['OUT']['mesh_distance_m']<.001 and 0<r['IN_to_first_tread_rise_m']<=.13
 elif o['role']=='bridge_bearer':
  i=Vector(sockets['IN']);e=Vector(sockets['OUT']);r['interpretation']='IN/OUT identify effective span boundaries on the beam bottom datum, not the visual bounding-box ends or deck-top height.';r['end_extensions_m']=[i.y-lo.y,hi.y-e.y];r['effective_span_m']=e.y-i.y;r['socket_to_bearer_top_m']=hi.z-i.z;r['pass']=all(s['mesh_distance_m']<.001 for s in r['sockets'].values()) and all(v>0 for v in r['end_extensions_m'])
 elif o['role']=='rock':
  p=Vector(sockets['BASE']);r['interpretation']='BASE is a local bottom-height/front-origin datum. It is not a vertex, contact point or proof of rock/ground contact; actual deepest-vertex embedding was separately tested.';r['BASE_z_minus_actual_bottom_m']=p.z-lo.z;r['pass']=abs(p.z-lo.z)<.001
 else:
  r['interpretation']='Sockets lie on actual local surface/edge geometry.';r['pass']=all(s['mesh_distance_m']<.001 for s in r['sockets'].values())
 out.append(r)
json.dump({'scope':'Local socket semantics compared with reopened mesh geometry; metadata treated as the subject of review, not expected geometry','modules':out,'pass':all(r['pass'] for r in out)},open(os.path.dirname(__file__)+'/Independent_Local_Sockets.json','w'),ensure_ascii=False,indent=2)
print(json.dumps(out,ensure_ascii=False))
