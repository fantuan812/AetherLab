import bpy,os,sys,argparse
from mathutils import Vector
p=argparse.ArgumentParser();p.add_argument('--output-image',default='Independent_Road_X_Top.png');a=p.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else []);out=os.path.abspath(a.output_image);os.makedirs(os.path.dirname(out),exist_ok=True)
s=bpy.context.scene;c=bpy.data.objects['CAM_Road_Top'];s.camera=c;c.location=(28,5,50);c.rotation_euler=(Vector((28,5,0))-c.location).to_track_quat('-Z','Y').to_euler();c.data.ortho_scale=15
s.render.engine='BLENDER_WORKBENCH';s.display.shading.light='STUDIO';s.display.shading.color_type='MATERIAL';s.display.shading.show_shadows=True;s.display.shading.show_cavity=True;s.display.shading.cavity_type='BOTH';s.render.resolution_x=1000;s.render.resolution_y=1000;s.render.resolution_percentage=100;s.render.filepath=out;bpy.ops.render.render(write_still=True)
