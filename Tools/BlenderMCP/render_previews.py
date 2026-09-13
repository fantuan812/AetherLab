import bpy
import json
import math
from pathlib import Path
from mathutils import Vector

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'Art/AetherLab'
bpy.ops.wm.open_mainfile(filepath=str(OUT/'AetherLab_Prototype.blend'))
scene=bpy.context.scene
try:
    prefs=bpy.context.preferences.addons['cycles'].preferences
    prefs.compute_device_type='OPTIX';prefs.get_devices()
    gpu=False
    for device in prefs.devices:
        device.use=device.type!='CPU'
        gpu=gpu or device.use
    if gpu:scene.cycles.device='GPU'
except Exception as exc: print('CPU render fallback',exc)
scene.cycles.samples=48
bpy.ops.render.render(write_still=True)

manifest=json.loads((OUT/'asset-manifest.json').read_text(encoding='utf-8'))
for ob in list(scene.objects):
    if not ob.name.startswith('SM_'): bpy.data.objects.remove(ob,do_unlink=True)
records=manifest['assets']
for i,entry in enumerate(records):
    ob=bpy.data.objects.get(entry['name']); ob.hide_render=False;ob.hide_set(False)
    row,col=divmod(i,7)
    # Each catalog cell has its own 2.5m presentation envelope; dimensions remain in manifest.
    scale=min(1,2.5/max(ob.dimensions))
    ob.scale=(scale,)*3;ob.location=(col*3.6,-row*4,0)
    data=bpy.data.curves.new('Asset label','FONT');data.body=f'{i+1:02d}  '+entry['name'][3:]
    data.size=.115;data.align_x='CENTER'
    label=bpy.data.objects.new('Asset label',data);scene.collection.objects.link(label)
    label.location=(col*3.6,-row*4-1.65,.025)
    data.materials.append(bpy.data.materials['M_Aether_Ceramic'])
    bpy.ops.mesh.primitive_cylinder_add(vertices=48,radius=1.53,depth=.075,location=(col*3.6,-row*4,-.10))
    bpy.context.object.data.materials.append(bpy.data.materials['M_Aether_DarkSteel'])
rows=math.ceil(len(records)/7);center=Vector((10.8,-(rows-1)*2,.1))
world=scene.world;world.node_tree.nodes.get('Background').inputs[1].default_value=.65
for loc,power,size,color in [((0,-10,18),11000,18,(1,.83,.67)),((20,-5,16),11000,15,(.57,.80,1)),((8,-30,15),9000,12,(.6,1,.9))]:
    data=bpy.data.lights.new('Catalog light','AREA');data.energy=power;data.size=size;data.color=color
    ob=bpy.data.objects.new('Catalog light',data);scene.collection.objects.link(ob);ob.location=loc
    ob.rotation_euler=(center-ob.location).to_track_quat('-Z','Y').to_euler()
data=bpy.data.cameras.new('Catalog camera');cam=bpy.data.objects.new('Catalog camera',data);scene.collection.objects.link(cam)
cam.location=center+Vector((3,-17,36));cam.rotation_euler=(center-cam.location).to_track_quat('-Z','Y').to_euler()
data.type='ORTHO';data.ortho_scale=35.5;scene.camera=cam
scene.cycles.samples=32;scene.render.resolution_x=2600;scene.render.resolution_y=3000
scene.render.filepath=str(OUT/'Previews/Asset_Catalog.png')
bpy.ops.render.render(write_still=True)
print('AETHER_PREVIEWS_COMPLETE')
