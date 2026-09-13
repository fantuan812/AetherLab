import bpy
import runpy
def build_aetherlab():
    runpy.run_path('C:/ueproject/test/Tools/BlenderMCP/build_assets.py', run_name='__main__')
    return None
bpy.app.timers.register(build_aetherlab, first_interval=1.0)
print('AetherLab build queued. Progress: Art/AetherLab/build-status.json')
