"""Reopen a saved world and produce evidence views without changing source."""
import bpy,os,json,argparse,sys,hashlib
p=argparse.ArgumentParser();p.add_argument('--output',required=True);p.add_argument('--only',default='all');p.add_argument('--samples',type=int,default=12);a=p.parse_args(sys.argv[sys.argv.index('--')+1:]);os.makedirs(a.output,exist_ok=True)
s=bpy.context.scene;s.render.engine='CYCLES';s.cycles.samples=a.samples;s.cycles.use_denoising=False;s.render.image_settings.file_format='PNG';s.render.resolution_percentage=100
ann=bpy.data.collections['10_ANNOTATIONS__NOT_GAMEPLAY']
for o in ann.objects:
 if hasattr(o,'visible_shadow'):o.visible_shadow=False
cams=[('CAM_Global_Axonometric','Global_Overview',1800,1400,False),('CAM_Global_Top','Global_Annotated_Top',2000,2000,True),('CAM_Town_Overview','Town_Scenes02_07',1500,1300,False),('CAM_Waterworks_Overview','Waterworks_Routes',1450,1250,False),('CAM_Abbey_Overview','Abbey_And_Hall',1450,1250,False),('CAM_Forest_Overview','Forest_Routes',1450,1250,False),('CAM_Relay_Overview','Relay_Routes',1450,1250,False)]
cams += [(n,n.replace('CAM_Ground_','Ground_'),1400,900,False) for n in ['CAM_Ground_SCN01','CAM_Ground_Gate','CAM_Ground_Plaza','CAM_Ground_Training','CAM_Ground_Academy','CAM_Ground_Shop','CAM_Ground_Inn','CAM_Ground_Forest','CAM_Ground_Waterworks','CAM_Ground_Maintenance','CAM_Ground_Abbey','CAM_Ground_Hall','CAM_Ground_Relay']]
rows=[]
for cam,n,w,h,annotated in cams:
 if a.only!='all' and a.only not in [n,cam] and not (a.only=='overview' and n in ['Global_Overview','Global_Annotated_Top']) and not (a.only=='ground' and n.startswith('Ground_')):continue
 s.camera=bpy.data.objects[cam];s.render.resolution_x=w;s.render.resolution_y=h;ann.hide_render=not annotated;s.render.filepath=os.path.join(a.output,n+'.png');bpy.ops.render.render(write_still=True);rows.append({'file':n+'.png','camera':cam,'resolution':[w,h],'annotated':annotated});print('RENDERED',n,flush=True)
json.dump({'source_sha256':hashlib.sha256(open(bpy.data.filepath,'rb').read()).hexdigest(),'views':rows},open(os.path.join(a.output,'Render_Receipt_'+a.only+'.json'),'w'),indent=2)
