"""Refresh local snap metadata. Uses no geometry changes; callable by builder or --python on reopened file."""
import bpy,json,os,hashlib,sys,argparse
from mathutils import Vector

def geometry_fingerprint():
 # Mesh coordinates, polygons, UV, material names, all object transforms and cameras: render-affecting snapshot.
 record=[]
 for o in sorted(bpy.context.scene.objects,key=lambda x:x.name):
  if o.type=='MESH':record.append((o.name,[list(row) for row in o.matrix_world],o.hide_render,[list(v.co) for v in o.data.vertices],[list(p.vertices) for p in o.data.polygons],[[list(d.uv) for d in layer.data] for layer in o.data.uv_layers],[m.name if m else None for m in o.data.materials]))
  elif o.type=='CAMERA':record.append((o.name,[list(row) for row in o.matrix_world],o.data.type,o.data.ortho_scale,o.data.lens))
 return hashlib.sha256(json.dumps(record,separators=(',',':')).encode()).hexdigest()

def refresh(root):
 before=geometry_fingerprint();masters=bpy.data.collections['01_MASTER_MODULES__4m_Candidate'];tech=bpy.data.collections['04_SOCKET_GUIDES'];interfaces=[]
 for o in list(tech.objects):bpy.data.objects.remove(o,do_unlink=True)
 for o in masters.objects:
  if o.type!='MESH':continue
  n=o.name
  if n=='KIT_LanternPost':sockets={'BASE':(0,0,0)}
  elif 'Pier' in n or n=='KIT_Fence_SharedPost':sockets={'CENTER':(0,0,0)}
  elif n=='KIT_Shoulder_Straight_1m':sockets={'ROAD':(0,.5,-.035),'EARTH':(1,.5,-.35),'SIDE_A':(.5,0,-.1925),'SIDE_B':(.5,1,-.1925)}
  elif 'Convex90' in n:sockets={'ROAD_CORNER':(0,0,-.035),'EDGE_X':(.5,0,-.1925),'EDGE_Y':(0,.5,-.1925),'EARTH_CORNER':(1,1,-.35)}
  elif 'Concave90' in n:sockets={'ROAD_CORNER':(0,0,-.035),'ROAD_X':(.5,0,-.035),'ROAD_Y':(0,.5,-.035),'EARTH_CORNER':(1,1,-.35)}
  elif 'Terrain_' in n:
   z=.965 if 'Raised' in n else -.35;sockets={'IN':(.5,0,z),'OUT':(.5,1,z),'LEFT':(0,.5,z),'RIGHT':(1,.5,z)}
  elif n=='KIT_Raised_EndBank_1m':sockets={'IN':(0,0,1),'OUT':(0,1,1)}
  elif n=='KIT_Raised_Landing_Bank_4m_Height1m':sockets={'IN':(0,0,1),'OUT':(0,4,1)}
  elif ('Rise1m' in n or '_Rise_4m_1m' in n):sockets={'IN':(0,0,0),'OUT':(0,4,1)}
  else:sockets={'IN':(0,0,0),'OUT':(0,4,0)}
  if n in ['KIT_Path_Flagstone_4x4_A','KIT_Path_Substrate_4x4','KIT_Path_Foundation_4x4']:sockets.update({'LEFT':(-2,2,0),'RIGHT':(2,2,0)})
  if n=='KIT_Fence_Rails_4m':sockets['MID']=(0,2,0)
  if n=='KIT_Fence_Rails_Rise_4m_1m':sockets['MID']=(0,2,.5)
  # Remove inherited misleading socket entries, then write explicit local coordinates.
  for k in ['IN','OUT','LEFT','RIGHT','MID','CENTER']:
   if k in o:del o[k]
  for k,v in sockets.items():
   o[k]=list(v);e=bpy.data.objects.new(n+'__'+k,None);tech.objects.link(e);e.parent=o;e.location=v;e.empty_display_type='ARROWS';e.empty_display_size=.18;e.hide_render=True
  o['connectors_json']=json.dumps(sockets);o['allowed_instance_scale']='1,1,1 only';o['forward_up']='+Y/+Z';o['socket_semantics']='nominal route grade for road/wall/fence/bank; explicit visible surface grade for 1m terrain/shoulder';o['connection_pitch_scope']='4m for this core candidate only; 1m edge/soil/endpoint submodules'
  vs=[v.co for v in o.data.vertices];interfaces.append({'asset_id':n,'local_sockets_m':sockets,'local_mesh_bounds_m':[[min(v[i] for v in vs) for i in range(3)],[max(v[i] for v in vs) for i in range(3)]],'origin':o['socket_semantics'],'forward':'+Y','up':'+Z','allowed_rotation':'multiples of90deg for tested fixtures; slope variants directional','allowed_scale':[1,1,1],'materials':[m.name for m in o.data.materials],'source':o.get('derived_from') or o.get('source_collection'),'geometry_change':'none in this metadata refresh'})
 after=geometry_fingerprint();assert before==after,'Metadata refresh changed render geometry'
 report={'version':2,'geometry_and_camera_fingerprint_sha256':before,'render_geometry_unchanged_after_metadata_refresh':before==after,'socket_coordinate_space':'master local meters; apply instance matrix_world to get world sockets','nominal_grade_vs_visual_bounds':'Socket grade0 may have top soil at-.035m; no assumption that socket is outer mesh boundary','interfaces':interfaces}
 os.makedirs(root+'/docs',exist_ok=True);json.dump(report,open(root+'/docs/Module_Interfaces.json','w'),ensure_ascii=False,indent=2)
 return report
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--output-root',required=True);a=p.parse_args(sys.argv[sys.argv.index('--')+1:]);root=os.path.abspath(a.output_root);r=refresh(root);bpy.ops.wm.save_as_mainfile(filepath=bpy.data.filepath,compress=True);print('METADATA_ONLY',r['geometry_and_camera_fingerprint_sha256'])
