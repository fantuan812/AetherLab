"""Supplementary technical fill-light evidence. Geometry remains visible and source is never saved."""
import bpy,os,json,hashlib,argparse,sys
p=argparse.ArgumentParser();p.add_argument('--output',required=True);p.add_argument('--expected-source-sha',required=True);p.add_argument('--only',default='all');p.add_argument('--wide-academy',action='store_true');a=p.parse_args(sys.argv[sys.argv.index('--')+1:]);os.makedirs(a.output,exist_ok=True);path=bpy.data.filepath;sha=hashlib.sha256(open(path,'rb').read()).hexdigest()
if sha!=a.expected_source_sha:raise RuntimeError('Frozen source mismatch')
s=bpy.context.scene;vis={o.name:o.hide_render for o in s.objects if o.type=='MESH'};rows=[];lights=[]
for n,pos,energy,size in [('Training',(-36,4,5.4),2000,8),('Academy',(34,6,4.1),1600,7),('Shop',(-35,-31,3.7),1400,6)]:
 d=bpy.data.lights.new('TECH_FILL_'+n,'AREA');d.energy=energy;d.shape='DISK';d.size=size;d.color=(1,.94,.84);o=bpy.data.objects.new('TECH_FILL_'+n,d);s.collection.objects.link(o);o.location=pos;lights.append({'name':o.name,'type':'AREA_DISK','position_m':list(pos),'energy_W':energy,'size_m':size,'color_rgb':[1,.94,.84],'rotation_euler':[0,0,0]})
s.render.engine='CYCLES';s.cycles.samples=32;s.cycles.use_denoising=False;s.render.image_settings.file_format='PNG';s.render.resolution_x=1400;s.render.resolution_y=900;s.render.resolution_percentage=100;bpy.data.collections['10_ANNOTATIONS__NOT_GAMEPLAY'].hide_render=True
for n in ['Training','Academy','Shop']:
 if a.only!='all' and a.only!=n:continue
 cam='CAM_Ground_'+n;out='Ground_'+n+('_TechWide.png' if a.wide_academy else '_TechFill.png');s.camera=bpy.data.objects[cam]
 if a.wide_academy and n=='Academy':
  from mathutils import Vector
  s.camera.location=(26.5,-.5,1.55);s.camera.rotation_euler=(Vector((39,6,1.8))-s.camera.location).to_track_quat('-Z','Y').to_euler();s.camera.data.lens=24
 s.render.filepath=os.path.join(a.output,out);bpy.ops.render.render(write_still=True);rows.append({'file':out,'camera':cam,'source_sha256':sha,'sha256':hashlib.sha256(open(s.render.filepath,'rb').read()).hexdigest(),'samples':32,'camera_position_m':list(s.camera.location),'lens_mm':s.camera.data.lens,'camera_rotation_euler':list(s.camera.rotation_euler),'interpretation':'TECHNICAL FILL LIGHTING ONLY; not the authored world-lighting state'})
end=hashlib.sha256(open(path,'rb').read()).hexdigest()
if end!=sha:raise RuntimeError('Source changed during supplemental render')
if vis!={o.name:o.hide_render for o in s.objects if o.type=='MESH'}:raise RuntimeError('Geometry visibility changed')
json.dump({'source_sha256':sha,'source_sha256_at_start':sha,'source_sha256_at_end':end,'source_unchanged':True,'all_geometry_visibility_unchanged':True,'temporary_lights':lights,'views':rows,'source_not_saved':True},open(os.path.join(a.output,'Render_Receipt_TechWide.json' if a.wide_academy else 'Render_Receipt_TechFill.json'),'w'),ensure_ascii=False,indent=2)
