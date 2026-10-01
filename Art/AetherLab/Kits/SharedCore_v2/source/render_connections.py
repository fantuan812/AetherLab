import bpy,os,sys,argparse,json
p=argparse.ArgumentParser();p.add_argument('--output-root',required=True);a=p.parse_args(sys.argv[sys.argv.index('--')+1:]);root=os.path.abspath(a.output_root);s=bpy.context.scene
meta=[]
for camera,name,w,h in [('CAM_Overview','CoreKit_Overview',1500,1200),('CAM_Connections_Close','CoreKit_Assembly_Close',1300,1100),('CAM_Road_Top','CoreKit_Road_Top',1200,1200),('CAM_Junction_Close','CoreKit_Junction',1100,950),('CAM_Height_Side','CoreKit_Height_Side',1300,850)]:
 s.camera=bpy.data.objects[camera];s.render.resolution_x=w;s.render.resolution_y=h;s.render.filepath=root+'/previews/'+name+'.png';bpy.ops.render.render(write_still=True)
 meta.append({'filename':name+'.png','camera':camera,'world_location':list(s.camera.location),'rotation':list(s.camera.rotation_euler),'ortho_scale':s.camera.data.ortho_scale,'resolution':[w,h],'source':os.path.basename(bpy.data.filepath),'scope':'isolated fixture; not world/engine/performance approval'})
json.dump(meta,open(root+'/docs/Preview_Cameras.json','w'),indent=2)
