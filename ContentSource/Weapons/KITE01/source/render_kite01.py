import bpy
from pathlib import Path
from mathutils import Vector
R=Path(__file__).resolve().parents[1]
bpy.ops.wm.open_mainfile(filepath=str(R/'KITE01_Modular.blend'))
s=bpy.context.scene;s.cycles.use_denoising=False;s.cycles.samples=40
s.render.filepath=str(R/'renders/KITE01_Beauty.png');bpy.ops.render.render(write_still=True)
c=s.camera;c.location=(.07,-2,.06);c.rotation_euler=(Vector((.07,0,.02))-c.location).to_track_quat('-Z','Y').to_euler();c.data.ortho_scale=1.08
s.render.filepath=str(R/'renders/KITE01_Side.png');bpy.ops.render.render(write_still=True)
