"""Render saved MCP-built scenes without altering their geometry."""
import bpy, json, traceback
from pathlib import Path
from mathutils import Vector
OUT=Path('C:/ueproject/test/Art/SwordMagic')
def render_all():
    try:
        for name,image in [('SM_02_Characters','02_Character_Lineup.png'),('SM_01_BrokenBellAbbey','01_Abbey_Overview.png')]:
            sc=bpy.data.scenes[name];bpy.context.window.scene=sc
            sc.render.filepath=str(OUT/'Previews'/image)
            (OUT/'render-status.json').write_text(json.dumps({'stage':'rendering','image':image}))
            bpy.ops.render.render(write_still=True)
        sc=bpy.data.scenes['SM_01_BrokenBellAbbey']
        old=sc.camera
        d=bpy.data.cameras.new('CAM_Courtyard_Detail');cam=bpy.data.objects.new(d.name,d);sc.collection.objects.link(cam)
        cam.location=(24,-37,20);cam.rotation_euler=(Vector((0,7,4))-cam.location).to_track_quat('-Z','Y').to_euler()
        d.type='PERSP';d.lens=36;sc.camera=cam
        sc.render.resolution_x=1800;sc.render.resolution_y=1200
        sc.render.filepath=str(OUT/'Previews/03_Courtyard_Detail.png')
        bpy.ops.render.render(write_still=True)
        sc.camera=old;sc.render.resolution_x=1800;sc.render.resolution_y=1400
        sc.render.filepath=str(OUT/'Previews/01_Abbey_Overview.png')
        bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'BrokenBellAbbey_Characters.blend'))
        (OUT/'render-status.json').write_text(json.dumps({'stage':'complete','images':3}))
    except Exception:
        (OUT/'render-status.json').write_text(json.dumps({'stage':'failed','error':traceback.format_exc()}));raise
    return None
bpy.app.timers.register(render_all,first_interval=.5)
print('Three real Blender renders queued through MCP.')
