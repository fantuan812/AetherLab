import bpy,runpy
def build_modular():
    runpy.run_path('C:/ueproject/test/Tools/BlenderMCP/prepare_modular_assets.py',run_name='__main__');return None
bpy.app.timers.register(build_modular,first_interval=.5)
print('Modular character/equipment export queued through Blender MCP.')
