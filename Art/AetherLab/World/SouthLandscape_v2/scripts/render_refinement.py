"""Fixed-camera A/B images from reopened frozen sources; never saves .blend."""
import bpy,json,sys,argparse,hashlib,os
from mathutils import Vector
p=argparse.ArgumentParser();p.add_argument('--output',required=True);p.add_argument('--samples',type=int,default=16);p.add_argument('--only',default='all');p.add_argument('--expected-sha',required=True);a=p.parse_args(sys.argv[sys.argv.index('--')+1:])
os.makedirs(a.output,exist_ok=True)
sha=lambda f:hashlib.sha256(open(f,'rb').read()).hexdigest()
src=bpy.data.filepath;start=sha(src)
if start!=a.expected_sha:raise RuntimeError('Unexpected source SHA')
s=bpy.context.scene;s.render.engine='CYCLES';s.cycles.samples=a.samples;s.cycles.use_denoising=False;s.render.resolution_percentage=100;s.render.image_settings.file_format='PNG'
bpy.data.collections['10_ANNOTATIONS__NOT_GAMEPLAY'].hide_render=True
specs=[('Global',None,None,'CAM_Global_Axonometric',1300,1050,False),('South_Overview',(190,-510,220),(-28,-218,7),None,1400,1000,330),('Entry',None,None,'CAM_Ground_SCN01',1200,780,False),('Ridge_Reveal',(-47.9,-291,14.48),(-27,-208,8.7),None,1200,780,False),('Town_Reveal',(-12,-160,5.55),(0,-80,4.8),None,1200,780,False),('Gate',None,None,'CAM_Ground_Gate',1200,780,False)]
rows=[]
for name,pos,target,existing,w,h,ortho in specs:
 if a.only!='all' and name not in a.only.split(','):continue
 if existing:cam=bpy.data.objects[existing]
 else:
  ca=bpy.data.cameras.new('CAM_COMPARE_'+name);cam=bpy.data.objects.new('CAM_COMPARE_'+name,ca);s.collection.objects.link(cam);cam.location=pos;cam.rotation_euler=(Vector(target)-cam.location).to_track_quat('-Z','Y').to_euler();ca.lens=35;ca.clip_end=3000
  if ortho:ca.type='ORTHO';ca.ortho_scale=ortho
 s.camera=cam;s.render.resolution_x=w;s.render.resolution_y=h;s.render.filepath=os.path.join(a.output,name+'.png');bpy.ops.render.render(write_still=True)
 rows.append({'file':name+'.png','sha256':sha(s.render.filepath),'camera':cam.name,'matrix_world':[list(row) for row in cam.matrix_world],'lens_mm':cam.data.lens,'ortho_scale':cam.data.ortho_scale if cam.data.type=='ORTHO' else None,'resolution':[w,h],'samples':a.samples,'source_sha256':start});print('RENDERED',name,flush=True)
end=sha(src)
if end!=start:raise RuntimeError('Source changed during rendering')
json.dump({'source_sha256_at_start':start,'source_sha256_at_end':end,'source_unchanged':True,'views':rows},open(os.path.join(a.output,'Render_Receipt_'+a.only.replace(',','_')+'.json'),'w'),indent=2)
