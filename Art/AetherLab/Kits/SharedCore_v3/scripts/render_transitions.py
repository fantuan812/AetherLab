import bpy,os,sys,argparse,json,hashlib
p=argparse.ArgumentParser();p.add_argument('--output-root',required=True);a=p.parse_args(sys.argv[sys.argv.index('--')+1:]);r=os.path.abspath(a.output_root);s=bpy.context.scene;rows=[]
for cam,out,w,h in [('CAM_v3_Overview','CoreKit_Overview',1250,1450),('CAM_v3_Stair','Transition_Stair',1100,900),('CAM_v3_Bridge','Transition_Bridge',1100,900),('CAM_v3_Rock','Transition_Rock',1000,850),('CAM_v3_Side','Transition_Side',1300,650)]:
 s.camera=bpy.data.objects[cam];s.render.resolution_x=w;s.render.resolution_y=h;s.render.image_settings.file_format='PNG';s.render.filepath=r+'/previews/'+out+'.png';bpy.ops.render.render(write_still=True)
 rows.append({'file':out+'.png','camera':cam,'position':list(s.camera.location),'rotation':list(s.camera.rotation_euler),'ortho_scale':s.camera.data.ortho_scale,'resolution':[w,h]})
json.dump({'source_sha256':hashlib.sha256(open(bpy.data.filepath,'rb').read()).hexdigest(),'scope':'v3 connected fixture, not world','cameras':rows},open(r+'/docs/Preview_Cameras.json','w'),indent=2)
