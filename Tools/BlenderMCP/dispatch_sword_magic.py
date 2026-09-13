import bpy, runpy
def build_sword_magic():
    runpy.run_path('C:/ueproject/test/Tools/BlenderMCP/build_sword_magic.py',run_name='__main__')
    return None
bpy.app.timers.register(build_sword_magic,first_interval=.5)
print('Sword-and-magic model build queued through Blender MCP; progress: Art/SwordMagic/build-status.json')
