"""Original procedural game assets for AetherLab. Blender 5.2 / metres / Z up.

Run through Blender MCP, or: blender -b --python Tools/BlenderMCP/build_assets.py
All exported geometry is authored here; no downloaded marketplace models.
"""
import bpy
import bmesh
import math
import random
import json
import traceback
from pathlib import Path
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'Art/AetherLab'
FBX = OUT / 'Exports/FBX'
GLB = OUT / 'Exports/GLB'
for p in (OUT, FBX, GLB, OUT / 'Previews'):
    p.mkdir(parents=True, exist_ok=True)
RNG = random.Random(91226)
PARTS, ASSETS, RECORDS, PLACEMENTS = [], {}, [], []
CURRENT = ''
PALETTE = {
    'Wood': ((0.26, .12, .045), 0, .8),
    'WoodLight': ((.44, .25, .105), 0, .75),
    'EndGrain': ((.19, .076, .025), 0, .88),
    'Charcoal': ((.018, .022, .022), 0, .95),
    'Steel': ((.19, .26, .27), .82, .4),
    'DarkSteel': ((.033, .058, .065), .78, .48),
    'Copper': ((.42, .205, .083), .82, .36),
    'Rust': ((.26, .085, .027), .35, .84),
    'TealPaint': ((.028, .23, .225), .45, .44),
    'OchrePaint': ((.66, .30, .045), .32, .53),
    'Concrete': ((.24, .29, .29), 0, .92),
    'StoneLight': ((.39, .43, .40), 0, .87),
    'Rubber': ((.015, .022, .025), 0, .78),
    'Water': ((.025, .24, .30), .35, .14),
    'Ice': ((.19, .56, .65), .12, .22),
    'Oil': ((.027, .017, .01), .24, .19),
    'Ceramic': ((.67, .70, .59), .0, .38),
    'GlowTeal': ((.035, .8, .63), .35, .22),
    'GlowAmber': ((1, .20, .025), .15, .25),
    'GlowBlue': ((.10, .4, 1), .15, .2),
}
MATS = {}

def status(stage, **kw):
    (OUT / 'build-status.json').write_text(json.dumps({'stage': stage, 'assets': len(RECORDS), **kw}, indent=2))
    print('AETHER_BUILD', stage, kw, flush=True)

def material(name):
    color, metal, rough = PALETTE[name]
    m = bpy.data.materials.new('M_Aether_' + name)
    m.diffuse_color = (*color, 1)
    m.use_nodes = True
    nodes, links = m.node_tree.nodes, m.node_tree.links
    bs = nodes.get('Principled BSDF')
    bs.inputs['Base Color'].default_value = (*color, 1)
    bs.inputs['Metallic'].default_value = metal
    bs.inputs['Roughness'].default_value = rough
    if name.startswith('Glow'):
        bs.inputs['Emission Color'].default_value = (*color, 1)
        bs.inputs['Emission Strength'].default_value = 3.0
    elif name not in ('Water', 'Ice', 'Oil'):
        tex = nodes.new('ShaderNodeTexNoise')
        tex.inputs['Scale'].default_value = 5.0
        tex.inputs['Detail'].default_value = 3
        coord = nodes.new('ShaderNodeTexCoord')
        mapping = nodes.new('ShaderNodeVectorMath'); mapping.operation = 'MULTIPLY'
        mapping.inputs[1].default_value = (3, 35, 5) if 'Wood' in name or name == 'EndGrain' else (4, 4, 4)
        links.new(coord.outputs['Generated'], mapping.inputs[0]); links.new(mapping.outputs[0], tex.inputs['Vector'])
        ramp = nodes.new('ShaderNodeValToRGB')
        ramp.color_ramp.elements[0].position = .15
        ramp.color_ramp.elements[0].color = (*(v * .53 for v in color), 1)
        ramp.color_ramp.elements[1].position = .85
        ramp.color_ramp.elements[1].color = (*(min(v * 1.35, 1) for v in color), 1)
        links.new(tex.outputs['Fac'], ramp.inputs[0]); links.new(ramp.outputs[0], bs.inputs['Base Color'])
        bump = nodes.new('ShaderNodeBump')
        bump.inputs['Strength'].default_value = .16
        bump.inputs['Distance'].default_value = .022
        links.new(tex.outputs['Fac'], bump.inputs['Height']); links.new(bump.outputs['Normal'], bs.inputs['Normal'])
    return m

def track(obj, mat, collision=True):
    obj.name = CURRENT + '_part'
    obj.data.materials.append(MATS[mat])
    obj['collision_part'] = collision
    PARTS.append(obj)
    return obj

def bevel(obj, width=.015, segments=2):
    if width:
        mod = obj.modifiers.new('Manufactured edges', 'BEVEL'); mod.width = width; mod.segments = segments
        bpy.context.view_layer.objects.active = obj
        bpy.ops.object.modifier_apply(modifier=mod.name)
    return obj

def box(loc, size, mat, radius=.012, rot=(0, 0, 0), collision=True):
    bpy.ops.mesh.primitive_cube_add(size=1, location=loc)
    ob = bpy.context.object; ob.dimensions = size
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    bevel(ob, min(radius, min(size) / 4))
    ob.rotation_euler = rot
    return track(ob, mat, collision)

def cyl(loc, radius, depth, mat, vertices=24, rot=(0, 0, 0), collision=True):
    bpy.ops.mesh.primitive_cylinder_add(vertices=vertices, radius=radius, depth=depth, location=loc, rotation=rot)
    ob = bpy.context.object; bevel(ob, min(.008, radius / 10, depth / 4))
    for f in ob.data.polygons: f.use_smooth = abs(f.normal.z) < .5
    return track(ob, mat, collision)

def sphere(loc, scale, mat, subdivisions=2, collision=True):
    bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=subdivisions, radius=1, location=loc)
    ob = bpy.context.object; ob.scale = scale
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    return track(ob, mat, collision)

def rod(a, b, radius, mat, vertices=12, collision=True):
    a, b = Vector(a), Vector(b)
    ob = cyl((a + b) / 2, radius, (b-a).length, mat, vertices, collision=collision)
    ob.rotation_euler = (b-a).to_track_quat('Z', 'Y').to_euler()
    return ob

def ring(loc, major, minor, mat, rot=(0, 0, 0), collision=False):
    bpy.ops.mesh.primitive_torus_add(major_segments=32, minor_segments=8, location=loc,
                                   major_radius=major, minor_radius=minor, rotation=rot)
    ob = bpy.context.object
    for p in ob.data.polygons: p.use_smooth = True
    return track(ob, mat, collision)

def mesh(name, verts, faces, mat, collision=True):
    data = bpy.data.meshes.new(name); data.from_pydata(verts, [], faces); data.update()
    ob = bpy.data.objects.new(name, data); bpy.context.collection.objects.link(ob)
    return track(ob, mat, collision)

def lathe(profile, mat, segments=32, loc=(0, 0, 0), collision=True):
    verts = [(loc[0]+r*math.cos(i*math.tau/segments), loc[1]+r*math.sin(i*math.tau/segments), loc[2]+z)
             for r, z in profile for i in range(segments)]
    faces = []
    for j in range(len(profile)-1):
        for i in range(segments):
            a=j*segments+i; b=j*segments+(i+1)%segments
            faces.append((a,b,b+segments,a+segments))
    faces += [tuple(reversed(range(segments))), tuple((len(profile)-1)*segments+i for i in range(segments))]
    ob = mesh('Turned profile', verts, faces, mat, collision)
    for f in ob.data.polygons[:-2]: f.use_smooth=True
    return ob

def bolt(loc, axis='Y', mat='Steel'):
    return cyl(loc, .024, .018, mat, 6, rot=(math.pi/2,0,0) if axis=='Y' else (0,0,0), collision=False)

def plank(loc, size, charred=False):
    mat = 'Charcoal' if charred else RNG.choice(['Wood','WoodLight','Wood'])
    ob = box(loc, size, mat, .009)
    # Shallow inset grain lines are geometry, so their scale survives FBX export.
    for i in range(3):
        x = loc[0] + RNG.uniform(-.37,.37)*size[0]
        length = size[2]*RNG.uniform(.3,.75)
        box((x,loc[1]-size[1]/2-.0008,loc[2]+RNG.uniform(-.08,.08)*size[2]),
            (.002,.0015,length), 'Charcoal' if charred else 'EndGrain', 0, collision=False)
    return ob

def begin(name):
    global CURRENT, PARTS
    CURRENT, PARTS = name, []
    bpy.ops.object.select_all(action='DESELECT')

def uv_unwrap(ob):
    bpy.ops.object.select_all(action='DESELECT'); ob.select_set(True)
    bpy.context.view_layer.objects.active=ob
    bpy.ops.object.mode_set(mode='EDIT'); bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.smart_project(angle_limit=math.radians(66), island_margin=.015)
    bpy.ops.object.mode_set(mode='OBJECT')
    ob.data.uv_layers[0].name='UV0'
    # Keep a second explicit, packed UV channel in the interchange files as well.
    ob.data.uv_layers.new(name='UV1_Lightmap',do_init=True)

def finish(label, category, preset, note='', fluid=False):
    status('building', current=CURRENT)
    collision=[]
    for i, source in enumerate([p for p in PARTS if p.get('collision_part') and not fluid]):
        bm=bmesh.new(); bm.from_mesh(source.data)
        hull=bmesh.ops.convex_hull(bm, input=list(bm.verts), use_existing_faces=False)
        # Export each closed convex part separately, preserving hollow assemblies.
        interior=[v for v in hull['geom_interior'] if isinstance(v,bmesh.types.BMVert) and v.is_valid]
        if interior: bmesh.ops.delete(bm,geom=interior,context='VERTS')
        data=bpy.data.meshes.new('UCX'); bm.to_mesh(data); bm.free()
        ob=bpy.data.objects.new(f'UCX_{CURRENT}_{i:02d}',data); bpy.context.collection.objects.link(ob)
        ob.matrix_world=source.matrix_world.copy(); collision.append(ob)
    bpy.ops.object.select_all(action='DESELECT')
    for ob in PARTS: ob.select_set(True)
    bpy.context.view_layer.objects.active=PARTS[0]; bpy.ops.object.convert(target='MESH'); bpy.ops.object.join()
    ob=bpy.context.object; ob.name=CURRENT
    bpy.context.scene.cursor.location=(0,0,0); bpy.ops.object.origin_set(type='ORIGIN_CURSOR')
    bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)
    # Ensure outward normals on all disconnected, closed pieces.
    bm=bmesh.new(); bm.from_mesh(ob.data); bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces)); bm.to_mesh(ob.data); bm.free()
    uv_unwrap(ob)
    ob['AetherPreset']=preset; ob['AssetLabel']=label; ob['AssetNotes']=note
    ob.asset_mark()
    for c in collision: c.select_set(True)
    bpy.ops.export_scene.fbx(filepath=str(FBX/(CURRENT+'.fbx')), use_selection=True, object_types={'MESH'},
        apply_unit_scale=True, apply_scale_options='FBX_SCALE_UNITS', axis_forward='-Y', axis_up='Z',
        bake_anim=False, use_mesh_modifiers=True, mesh_smooth_type='FACE', add_leaf_bones=False)
    for c in collision: bpy.data.objects.remove(c,do_unlink=True)
    bpy.ops.object.select_all(action='DESELECT'); ob.select_set(True)
    bpy.ops.export_scene.gltf(filepath=str(GLB/(CURRENT+'.glb')), export_format='GLB', use_selection=True,
        export_materials='EXPORT', export_animations=False)
    ob.data.calc_loop_triangles()
    RECORDS.append({'name':CURRENT,'label':label,'category':category,'preset':preset,'notes':note,
        'dimensions_m':[round(v,4) for v in ob.dimensions], 'triangles':len(ob.data.loop_triangles),
        'vertices':len(ob.data.vertices),'collision_hulls':len(collision),'fluid':fluid,
        'material_slots':[m.name for m in ob.data.materials], 'fbx':f'Exports/FBX/{CURRENT}.fbx', 'glb':f'Exports/GLB/{CURRENT}.glb'})
    ASSETS[CURRENT]=ob; ob.hide_render=True; ob.hide_set(True)
    return ob

def wood_assets():
    begin('SM_WoodPlank_200'); plank((0,0,1),(.24,.065,2)); finish('木板 2m','Structure','Wood')
    begin('SM_WoodBeam_300'); plank((0,0,1.5),(.22,.22,3)); finish('承重木梁 3m','Structure','Wood')
    begin('SM_WoodWall_200')
    for i in range(8): plank((-.875+i*.25,0,1.25),(.24,.09,2.5))
    for z in (.18,2.32):
        box((0,.085,z),(2,.1,.16),'WoodLight')
        for x in (-.8,.8): bolt((x,-.055,z))
    finish('木墙 2×2.5m','Structure','Wood','Whole panel; use individual beams/planks for independent structural reactions.')
    begin('SM_WoodDoor_100')
    for i in range(5): plank((-.4+i*.2,0,1.05),(.195,.07,2.1))
    for z in (.25,1.85):
        box((0,-.052,z),(.96,.034,.12),'DarkSteel')
        for x in (-.39,.39): bolt((x,-.08,z))
    ring((.31,-.115,1.05),.065,.012,'Copper',rot=(math.pi/2,0,0))
    finish('木门','Structure','Wood','Ground-centre pivot; set hinge relative offset -50cm in a door actor.')
    begin('SM_WoodPlatform_200')
    for i in range(8): box((-.875+i*.25,0,.19),(.24,2,.10),'WoodLight')
    for x in (-.72,.72): box((x,0,.075),(.18,2,.15),'Wood')
    finish('可燃平台 2m','Structure','Wood')
    begin('SM_WoodStair_200')
    for i in range(5): box((0,-.8+i*.4,.1+i*.2),(1.2,.42,.2),'WoodLight')
    for x in (-.46,.46): box((x,0,.46),(.15,2.12,.15),'Wood',rot=(math.atan(.5),0,0))
    finish('木台阶','Structure','Wood')
    begin('SM_WoodCrate_090')
    for i in range(5):
        x=-.352+i*.176
        for y in (-.415,.415): plank((x,y,.45),(.17,.07,.86))
        for xx in (-.415,.415): box((xx,x,.45),(.07,.17,.86),'Wood')
        box((x,0,.885),(.17,.83,.06),'WoodLight')
    for z in (.09,.81):
        for y in (-.46,.46): box((0,y,z),(.94,.065,.10),'WoodLight')
    box((0,-.5,.45),(.08,.04,1.05),'WoodLight',rot=(0,-.72,0))
    for x in (-.39,.39):
        for z in (.09,.81): bolt((x,-.5,z))
    finish('木箱','Props','Wood')
    begin('SM_WoodBarrel')
    lathe([(.31,0),(.34,.12),(.39,.48),(.34,.85),(.31,.94)],'Wood')
    for z,r in ((.1,.339),(.27,.375),(.7,.369),(.87,.335)): ring((0,0,z),r,.025,'DarkSteel')
    cyl((0,0,.942),.3,.022,'WoodLight'); finish('木桶','Props','Wood')
    begin('SM_WoodLogPile')
    for i,(x,z) in enumerate(((-.24,.18),(.17,.17),(0,.44))):
        rod((x,-.62,z),(x,.62,z),.17,'Wood',16)
        cyl((x,-.626,z),.145,.012,'WoodLight',16,rot=(math.pi/2,0,0),collision=False)
    finish('薪柴堆','Props','Wood')
    begin('SM_WoodBeam_Charred'); plank((0,0,1.5),(.205,.205,3),True)
    finish('炭化木梁','Damage','Wood','Mesh swap at high BurnAmount; same pivot as intact beam.')
    for i in range(3):
        begin(f'SM_WoodSplinter_{i+1:02d}')
        length=.45+i*.3
        mesh('Split timber',[(-.06,-.045,0),(.06,-.045,.04),(.07,.045,0),(-.05,.045,.02),
            (-.045,-.03,length),(.02,-.03,length+.17),(.03,.03,length-.07),(-.02,.03,length+.09)],
            [(0,3,2,1),(0,1,5,4),(1,2,6,5),(2,3,7,6),(3,0,4,7),(4,5,6,7)],'WoodLight' if i<2 else 'Charcoal')
        finish('木碎片 '+str(i+1),'Damage','Wood')

def architectural_assets():
    begin('SM_ConcreteFloor_400'); box((0,0,-.12),(4,4,.24),'Concrete',.035)
    for x in (-1.98,1.98): box((x,0,.003),(.018,3.96,.008),'DarkSteel',0,collision=False)
    finish('混凝土地板 4m','Structure','Stone','Walking surface at Z=0; 4m grid.')
    begin('SM_ConcreteWall_400'); box((0,0,1.5),(4,.24,3),'Concrete',.035)
    box((0,-.145,.26),(4,.05,.28),'DarkSteel')
    for x in (-1.78,1.78): box((x,0,1.5),(.23,.36,3),'StoneLight')
    finish('混凝土隔墙','Structure','Stone','Blocks Visibility to prevent heat/electric exchange through walls.')
    begin('SM_MetalColumn_300')
    box((0,0,1.5),(.18,.20,3),'DarkSteel')
    for y in (-.125,.125): box((0,y,1.5),(.34,.055,3),'Steel')
    for z in (.04,2.96):
        box((0,0,z),(.46,.46,.08),'Rust')
        for x in (-.17,.17):
            for y in (-.17,.17): bolt((x,y,z+.047),'Z')
    finish('工字钢柱 3m','Structure','Metal')
    begin('SM_MetalRoof_200')
    verts=[]; faces=[]
    for j in range(2):
        for i in range(49): verts.append((-1+i/24,-1+j*2,.10+.035*math.cos(i*math.pi/2)))
    for i in range(48): faces.append((i,i+1,50+i,49+i))
    ob=mesh('Corrugated sheet',verts,faces,'TealPaint',True)
    mod=ob.modifiers.new('Sheet thickness','SOLIDIFY'); mod.thickness=.014
    bpy.context.view_layer.objects.active=ob; bpy.ops.object.modifier_apply(modifier=mod.name)
    for y in (-.88,.88): box((0,y,.045),(2,.075,.075),'Rust')
    finish('波纹金属屋顶 2m','Structure','Metal')
    begin('SM_MetalGrating_200')
    for x in (-.96,.96): box((x,0,.06),(.08,2,.12),'DarkSteel')
    for y in (-.96,.96): box((0,y,.06),(2,.08,.12),'DarkSteel')
    for i in range(12): box((-.88+i*.16,0,.09),(.035,1.92,.07),'Steel',.004)
    for y in (-.65,0,.65): box((0,y,.045),(1.9,.04,.045),'Steel',.003)
    finish('金属格栅 2m','Structure','Metal')
    begin('SM_MetalRailing_200')
    for x in (-.92,.92):
        cyl((x,0,.57),.035,1.14,'Steel')
        box((x,0,.03),(.17,.17,.06),'DarkSteel')
    for z in (.54,1.13): rod((-1,0,z),(1,0,z),.035,'Steel')
    finish('导电栏杆','Structure','Metal')
    begin('SM_RailSegment_300')
    for y in (-1.25,-.625,0,.625,1.25): box((0,y,.065),(1.65,.17,.13),'Wood')
    for x in (-.52,.52):
        box((x,0,.16),(.14,3,.06),'Rust')
        box((x,0,.22),(.045,3,.11),'Steel')
        box((x,0,.29),(.1,3,.065),'Steel')
    finish('废弃导电轨道','Structure','Metal','Wood sleepers are decorative; rails provide the conductive body.')
    for i in range(3):
        begin(f'SM_ConcreteRubble_{i+1:02d}')
        ob=sphere((0,0,.18+i*.09),(.35+i*.12,.3+i*.1,.25+i*.07),'Concrete',1)
        for v in ob.data.vertices:
            v.co.x*=RNG.uniform(.85,1.15); v.co.y*=RNG.uniform(.85,1.15)
        finish('混凝土碎块 '+str(i+1),'Damage','Stone')

def industrial_assets():
    for variant,mat in (('Oil','OchrePaint'),('Water','TealPaint'),('Burst','Rust')):
        begin('SM_Drum_'+variant)
        profile=[(.29,.025),(.32,.07),(.305,.12),(.305,.28),(.325,.30),(.325,.335),(.305,.36),
                 (.305,.58),(.325,.61),(.325,.645),(.305,.68),(.305,.85),(.32,.9)]
        if variant=='Burst':
            # Open, torn lip; no intact top disk.
            verts=[]
            for r,z in profile:
                for i in range(32):
                    torn=RNG.uniform(-.13,.13) if z==.9 else 0
                    verts.append(((r+max(0,torn))*math.cos(i*math.tau/32),(r+max(0,torn))*math.sin(i*math.tau/32),z+torn))
            faces=[(j*32+i,j*32+(i+1)%32,(j+1)*32+(i+1)%32,(j+1)*32+i) for j in range(len(profile)-1) for i in range(32)]
            ob=mesh('Burst shell',verts,faces,mat,False)
            mod=ob.modifiers.new('Shell','SOLIDIFY');mod.thickness=.012
            bpy.context.view_layer.objects.active=ob;bpy.ops.object.modifier_apply(modifier=mod.name)
            cyl((0,0,.05),.29,.035,'DarkSteel')
        else:
            lathe(profile,mat)
            cyl((0,0,.895),.295,.019,'DarkSteel')
            cyl((.16,0,.925),.046,.035,'Steel',16)
            box((0,-.311,.49),(.22,.012,.20),'Ceramic',.005,collision=False)
            box((0,-.322,.49),(.08,.008,.08),'Rubber',.003,rot=(0,math.pi/4,0),collision=False)
        for z in (.065,.9): ring((0,0,z),.314,.011,'Rust')
        finish({'Oil':'油桶','Water':'水桶','Burst':'破裂油桶'}[variant],'Damage' if variant=='Burst' else 'Props',
               'Oil' if variant=='Oil' else 'Metal','Oil drum is a gameplay proxy for contained oil; mesh does not simulate fluid.')
    begin('SM_WaterBucket')
    lathe([(.19,0),(.22,.03),(.26,.43),(.24,.44),(.20,.07)],'Steel',24)
    ring((0,0,.44),.249,.018,'Copper')
    # Upright half-loop handle, leaving the mouth visibly open.
    for i in range(12):
        a=math.pi*i/12; b=math.pi*(i+1)/12
        rod((.26*math.cos(a),0,.40+.26*math.sin(a)),(.26*math.cos(b),0,.40+.26*math.sin(b)),.012,'DarkSteel',8,False)
    finish('提水桶','Props','Metal')
    begin('SM_WaterTank')
    for x in (-.55,.55): box((x,0,.2),(.12,.64,.4),'DarkSteel')
    ob=lathe([(.38,0),(.46,.15),(.46,1.18),(.38,1.32)],'TealPaint',32)
    ob.rotation_euler=(0,math.pi/2,0);ob.location=(-.66,0,.86)
    for x in (-.46,.46): ring((x,0,.86),.467,.027,'Steel',rot=(0,math.pi/2,0))
    rod((.42,-.31,.62),(.42,-.65,.62),.055,'Copper')
    ring((.42,-.62,.76),.10,.015,'OchrePaint',rot=(math.pi/2,0,0))
    finish('储水罐','Props','Metal','Use separate water actor or InitialWaterKg for its contents.')
    begin('SM_PressureVessel')
    for x in (-.2,.2): box((x,0,.12),(.10,.4,.24),'DarkSteel')
    lathe([(.24,.15),(.34,.3),(.34,1.05),(.23,1.22),(.09,1.27)],'Copper')
    for z in (.32,1.02): ring((0,0,z),.349,.025,'DarkSteel')
    cyl((0,0,1.32),.09,.12,'Steel')
    ring((0,0,1.41),.145,.021,'OchrePaint')
    cyl((0,-.355,.88),.105,.065,'DarkSteel',24,rot=(math.pi/2,0,0))
    cyl((0,-.39,.88),.083,.008,'Ceramic',24,rot=(math.pi/2,0,0),collision=False)
    box((.018,-.40,.898),(.008,.005,.098),'Rubber',0,rot=(0,-.55,0),collision=False)
    finish('密闭压力罐','Props','Metal','Configure a ReactiveMaterialAsset with SealedVolumeM3 and initial water for steam pressure.')
    begin('SM_PipeStraight_200')
    rod((0,0,.16),(0,2,.16),.11,'Rust',24)
    for y in (0,1,2): cyl((0,y,.16),.17,.07,'Steel',24,rot=(math.pi/2,0,0))
    finish('直管 2m','Structure','Metal','Start connector pivot at Y=0, pipe axis Z=16cm.')
    begin('SM_PipeElbow_090')
    for i in range(8):
        a=i*math.pi/16; b=(i+1)*math.pi/16
        rod((.5-.5*math.cos(a),.5*math.sin(a),.17),(.5-.5*math.cos(b),.5*math.sin(b),.17),.11,'Rust',16)
    cyl((0,0,.17),.17,.07,'Steel',24,rot=(math.pi/2,0,0))
    cyl((.5,.5,.17),.17,.07,'Steel',24,rot=(0,math.pi/2,0))
    finish('直角管','Structure','Metal')
    begin('SM_ConduitCable_300')
    for i in range(16):
        y=i*3/16; yy=(i+1)*3/16
        rod((.05*math.sin(y*3),y,.034),(.05*math.sin(yy*3),yy,.034),.024,'Rubber',8)
    for y in (0,3): cyl((.05*math.sin(y*3),y,.034),.042,.14,'Copper',12,rot=(math.pi/2,0,0))
    finish('裸露导电缆线','Props','Metal')
    begin('SM_ElectrodePylon')
    box((0,0,.055),(.64,.64,.11),'DarkSteel')
    cyl((0,0,.5),.085,.84,'Copper')
    for z in (.3,.42,.54,.66,.78): cyl((0,0,z),.145,.042,'Ceramic')
    sphere((0,0,1.0),(.17,.17,.17),'Steel',2)
    ring((0,0,.18),.22,.02,'GlowTeal')
    finish('导电电极','Mechanisms','Metal')
    begin('SM_RelayConsole')
    box((0,0,.13),(.75,.6,.26),'DarkSteel')
    box((0,.03,.73),(.58,.42,1.08),'TealPaint',.035)
    box((0,-.225,1.04),(.45,.052,.29),'DarkSteel')
    box((0,-.255,1.04),(.36,.012,.19),'GlowTeal',.006,collision=False)
    for x in (-.14,0,.14): cyl((x,-.25,.78),.038,.045,'Copper',12,rot=(math.pi/2,0,0))
    for z in (.40,.45,.50,.55): box((0,-.187,z),(.34,.02,.018),'Rubber',.002,collision=False)
    finish('遗迹控制台','Mechanisms','Metal')
    begin('SM_AetherReactor')
    cyl((0,0,.12),.8,.24,'DarkSteel',32)
    cyl((0,0,.30),.64,.12,'Copper',32)
    for i in range(6):
        a=i*math.tau/6; x,y=.48*math.cos(a),.48*math.sin(a)
        box((x,y,.92),(.16,.16,1.16),'TealPaint',.015,rot=(0,0,a))
    cyl((0,0,1.54),.66,.16,'DarkSteel',32)
    sphere((0,0,.96),(.25,.25,.45),'GlowTeal',2)
    for z in (.5,.93,1.35): ring((0,0,z),.39,.025,'Copper')
    ring((0,0,1.67),.5,.023,'GlowTeal')
    finish('以太反应堆','Mechanisms','Metal')
    begin('SM_Brazier')
    for i in range(3):
        a=i*math.tau/3; rod((.28*math.cos(a),.28*math.sin(a),0),(.16*math.cos(a),.16*math.sin(a),.42),.038,'DarkSteel')
    lathe([(.19,.30),(.34,.50),(.37,.59),(.34,.60),(.16,.36)],'Rust')
    for x in (-.09,.09): rod((x,-.2,.48),(x,.2,.48),.06,'Charcoal')
    finish('实验火盆','Props','Metal','Attach Niagara fire to a separate heat source actor.')

def liquid_assets():
    # Matching irregular footprint for the frozen/unfrozen state pair.
    radii=[1+.09*math.sin(i*math.tau/40*3+.4)+.05*math.sin(i*math.tau/40*5+1.8) for i in range(40)]
    for name,mat,z in (('WaterPuddle','Water',.025),('IcePuddle','Ice',.09),('OilSpill','Oil',.012)):
        begin('SM_'+name)
        verts=[]
        for height in (0,z):
            for i,r in enumerate(radii):
                a=i*math.tau/40;verts.append((r*math.cos(a)*1.25,r*math.sin(a)*.85,height))
        faces=[tuple(reversed(range(40))),tuple(40+i for i in range(40))]
        faces.extend((i,(i+1)%40,(i+1)%40+40,i+40) for i in range(40))
        mesh(name,verts,faces,mat)
        if mat=='Ice':
            for a,b in (((-.6,-.4,z),(.5,.5,z)),((.1,.1,z),(.7,-.3,z)),((-.35,-.15,z),(-.6,.5,z))):
                rod(a,b,.006,'StoneLight',5,False)
        finish({'Water':'浅水洼','Ice':'冻结水洼','Oil':'泄漏油面'}[mat],'Liquids','Oil' if mat=='Oil' else 'Water',
            'Matching water/ice footprint; water and oil disable blocking collision.',fluid=mat!='Ice')
    begin('SM_IceWall_200')
    for i in range(7):
        x=-.84+i*.28; h=1.65+RNG.uniform(-.2,.35)
        ob=sphere((x,0,h/2),(.25,.28,h/2),'Ice',1)
    finish('冰墙','Liquids','Water','Use ice initial state; separate from shallow-water collision.')
    begin('SM_IceBridge_300')
    box((0,0,.10),(1.6,3,.2),'Ice',.065)
    for x in (-.72,.72):
        for y in (-1.2,-.4,.4,1.2): sphere((x,y,.16),(.13,.3,.14),'Ice',1,False)
    finish('冰桥','Liquids','Water')
    for i in range(3):
        begin(f'SM_IceShard_{i+1:02d}');sphere((0,0,.15+i*.08),(.14+i*.03,.10+i*.03,.19+i*.08),'Ice',1)
        finish('冰碎片 '+str(i+1),'Damage','Water')

def character_and_spell_assets():
    # Articulated-looking static targets are intentionally not advertised as animated characters.
    for kind in ('Metal','Wood'):
        begin('SM_'+('SentinelTarget' if kind=='Metal' else 'WoodGolemTarget'))
        main='TealPaint' if kind=='Metal' else 'Wood'; edge='DarkSteel' if kind=='Metal' else 'WoodLight'
        for side in (-1,1):
            x=side*.18
            box((x,-.065,.10),(.23,.38,.20),edge,.025)
            rod((x,0,.21),(x,0,.69),.09,main)
            sphere((x,0,.76),(.115,.115,.115),'Copper' if kind=='Metal' else edge,2)
            rod((x,0,.82),(x,0,1.10),.105,main)
            sphere((side*.4,0,1.52),(.15,.14,.14),edge,2)
            rod((side*.44,0,1.48),(side*.52,-.02,1.18),.09,main)
            sphere((side*.52,-.02,1.14),(.095,.095,.095),'Copper' if kind=='Metal' else edge,2)
            rod((side*.52,-.02,1.10),(side*.56,-.10,.87),.08,main)
            box((side*.56,-.1,.83),(.15,.16,.16),edge,.02)
        box((0,0,1.12),(.46,.29,.22),edge,.04)
        box((0,0,1.40),(.62,.34,.45),main,.055)
        sphere((0,-.20,1.43),(.09,.038,.09),'GlowTeal' if kind=='Metal' else 'GlowAmber',2,False)
        cyl((0,0,1.69),.075,.14,edge)
        box((0,0,1.86),(.30,.28,.30),edge,.05)
        box((0,-.147,1.89),(.23,.024,.045),'GlowTeal' if kind=='Metal' else 'GlowAmber',.008,collision=False)
        finish('金属哨兵靶' if kind=='Metal' else '木制巨像靶','Targets',kind,
            'Static 2m gameplay test target; not a rigged or animated production character.')
    begin('SM_AetherStaff')
    cyl((0,0,.74),.027,1.48,'DarkSteel',16)
    for z in (.12,.16,.20,.7,.75,.8): ring((0,0,z),.034,.006,'Copper')
    cyl((0,0,1.52),.07,.13,'Copper',16)
    for i in range(3):
        a=i*math.tau/3
        rod((.05*math.cos(a),.05*math.sin(a),1.55),(.12*math.cos(a),.12*math.sin(a),1.84),.022,'Steel')
    sphere((0,0,1.76),(.085,.085,.16),'GlowTeal',2)
    finish('以太法杖','SpellMeshes','Metal','Grip/socket at Z=75cm; static equip mesh.')
    begin('SM_AetherGauntlet')
    box((0,0,.08),(.22,.30,.16),'DarkSteel',.03)
    box((0,-.17,.085),(.24,.18,.13),'TealPaint',.025)
    for x in (-.075,-.025,.025,.075): box((x,-.30,.08),(.04,.12,.065),'Steel',.015)
    box((.145,-.14,.065),(.075,.12,.08),'Steel',.015,rot=(0,0,-.5))
    sphere((0,-.1,.18),(.065,.065,.035),'GlowTeal',2,False)
    finish('施法护手','SpellMeshes','Metal','Static first-person equipment proxy; attach to an existing hand socket.')
    for name,mat in (('HeatCore','GlowAmber'),('WaterCore','Water'),('IceCore','Ice'),('ChargeCore','GlowBlue')):
        begin('SM_Spell_'+name);sphere((0,0,0),(.12,.12,.12),mat,2)
        finish('法术核心 '+name,'SpellMeshes','Water' if mat in ('Water','Ice') else 'Metal',
            'Centre pivot. Optional projectile/Niagara mesh; no particles baked into geometry.',fluid=True)
    begin('SM_RuneRing')
    ring((0,0,.01),.6,.015,'GlowTeal');ring((0,0,.01),.48,.008,'Copper')
    for i in range(12):
        a=i*math.tau/12
        box((.54*math.cos(a),.54*math.sin(a),.01),(.07,.024,.015),'GlowTeal',.003,rot=(0,0,a),collision=False)
    finish('施法环','SpellMeshes','Metal','Visual-only ground indicator.',fluid=True)

def instance(name, loc, rot=0, scale=(1,1,1)):
    source=ASSETS[name];ob=source.copy();ob.data=source.data
    bpy.context.scene.collection.objects.link(ob);ob.name='Scene_'+name[3:]
    ob.hide_render=False;ob.hide_set(False);ob.location=loc;ob.rotation_euler=(0,0,rot);ob.scale=scale
    PLACEMENTS.append({'asset':name,'location_m':list(loc),'yaw_degrees':math.degrees(rot),'scale':list(scale)})
    return ob

def text_obj(text,loc,size=.25,mat='Ceramic',rot=(0,0,0)):
    data=bpy.data.curves.new('Label','FONT');data.body=text;data.size=size;data.extrude=.0005
    ob=bpy.data.objects.new('Sign_'+text,data);bpy.context.scene.collection.objects.link(ob)
    ob.location=loc;ob.rotation_euler=rot;data.materials.append(MATS[mat]);return ob

def aim(ob,target): ob.rotation_euler=(Vector(target)-ob.location).to_track_quat('-Z','Y').to_euler()

def assemble():
    status('assembling')
    # An open-sided industrial test yard, all placements also consumed by the UE import script.
    for x in (-6,-2,2,6):
        for y in (-4,0,4): instance('SM_ConcreteFloor_400',(x,y,0))
    for x in (-6,-2,2,6): instance('SM_ConcreteWall_400',(x,5.9,0))
    for y in (-4,0,4): instance('SM_ConcreteWall_400',(-7.9,y,0),math.pi/2)
    for x in (-7.65,-3.8,0,3.8,7.65): instance('SM_MetalColumn_300',(x,5.65,0))
    # Burnable hut: discrete structure, metal roof, open front for visibility.
    for x in (-5,-3):
        for y in (1,3): instance('SM_WoodPlatform_200',(x,y,.1))
    for x in (-5.85,-2.15):
        for y in (.15,3.85): instance('SM_WoodBeam_300',(x,y,.24))
    for x in (-5,-3): instance('SM_WoodWall_200',(x,3.82,.25))
    instance('SM_WoodWall_200',(-5.83,1,.25),math.pi/2)
    for x in (-5,-3):
        for y in (1,3): instance('SM_MetalRoof_200',(x,y,3.24))
    instance('SM_WoodStair_200',(-4,-1,.0),scale=(1,1,.36))
    instance('SM_WoodCrate_090',(-4.9,1,.35),.18)
    instance('SM_WoodCrate_090',(-3.7,2.6,.35),-.2)
    instance('SM_WoodLogPile',(-3,1,.36))
    instance('SM_WoodGolemTarget',(-4.2,1.8,.35))
    instance('SM_Drum_Oil',(-1.4,2.8,0))
    instance('SM_Drum_Oil',(-.65,3.5,0),.3)
    instance('SM_OilSpill',(-1.1,2.9,.003),scale=(.7,.7,1))
    instance('SM_Brazier',(-2,-.2,.01))
    # Conductive test loop.
    for y in (-3,0,3): instance('SM_RailSegment_300',(2.2,y,0))
    instance('SM_WaterPuddle',(2.1,-2.2,.012),scale=(1.2,1.2,1))
    for x,y in ((1.4,-2),(3.1,-1.3),(2.8,1.0)): instance('SM_ElectrodePylon',(x,y,0))
    instance('SM_SentinelTarget',(2.25,.1,.32))
    instance('SM_RelayConsole',(3.8,4.1,0))
    instance('SM_AetherReactor',(5.6,4,0))
    instance('SM_ConduitCable_300',(3.6,1,0))
    # Freeze / pressure bench.
    instance('SM_WaterTank',(6.2,.8,0),-.2)
    instance('SM_Drum_Water',(6.9,1.8,0))
    instance('SM_PressureVessel',(5,1.4,0))
    instance('SM_WaterBucket',(5.4,-.1,0))
    instance('SM_IcePuddle',(5.7,-2.1,.01))
    instance('SM_IceWall_200',(6.5,-3.4,0),-.3,scale=(.8,.8,.8))
    instance('SM_IceBridge_300',(5.0,-4.9,0),math.pi/2,scale=(.75,.7,1))
    for i in range(3): instance(f'SM_IceShard_{i+1:02d}',(6+i*.25,-4.4,0),i)
    for i in range(3): instance(f'SM_ConcreteRubble_{i+1:02d}',(-6.8+i*.6,-3.3,0),i*.7)
    instance('SM_Drum_Burst',(-6.2,-4.4,0),.8)
    for i in range(3): instance(f'SM_WoodSplinter_{i+1:02d}',(-5.1+i*.27,-3.8,.03),i)
    instance('SM_AetherStaff',(-.4,-4.7,0))
    instance('SM_RuneRing',(0,-4.2,.01))
    # Blender-only typography; no text geometry is required for gameplay.
    text_obj('A E T H E R   /   FIELD LAB',(-7.2,-5.62,.018),.43,'Ceramic')
    for t,x in (('01 / THERMAL',-6.8),('02 / CONDUCTION',.3),('03 / PHASE',4.45)):
        text_obj(t,(x,-3.8,.02),.21,'OchrePaint')
    text_obj('REACTIVE WORLD  /  PROTOTYPE 01',(-6.8,5.745,2.65),.20,'Ceramic',rot=(math.pi/2,0,0))
    scene=bpy.context.scene
    scene.world.color=(.10,.10,.10);scene.world.use_nodes=True
    scene.world.node_tree.nodes.get('Background').inputs[0].default_value=(.105,.16,.20,1)
    scene.world.node_tree.nodes.get('Background').inputs[1].default_value=.45
    for name,loc,power,size,color in [('Key',(-5,-7,13),2600,10,(1,.78,.55)),('Fill',(7,-1,10),2200,8,(.43,.75,1)),('Rim',(0,7,11),2800,7,(.63,1,.88))]:
        data=bpy.data.lights.new(name,'AREA');data.energy=power;data.shape='DISK';data.size=size;data.color=color
        ob=bpy.data.objects.new(name,data);scene.collection.objects.link(ob);ob.location=loc;aim(ob,(0,0,0))
    data=bpy.data.cameras.new('PrototypeCamera');cam=bpy.data.objects.new('PrototypeCamera',data);scene.collection.objects.link(cam)
    cam.location=(18,-24,22);aim(cam,(0,0.3,.45));data.type='ORTHO';data.ortho_scale=23;scene.camera=cam
    scene.render.engine='CYCLES';scene.cycles.samples=40;scene.cycles.use_denoising=True
    scene.render.resolution_x=1800;scene.render.resolution_y=1400;scene.render.resolution_percentage=100
    scene.view_settings.view_transform='AgX'
    scene.render.image_settings.file_format='PNG';scene.render.filepath=str(OUT/'Previews/Prototype_Yard.png')
    for screen in bpy.data.screens:
        for area in screen.areas:
            if area.type=='VIEW_3D':
                area.spaces.active.region_3d.view_distance=25
                area.spaces.active.region_3d.view_location=(0,0,1)
                area.spaces.active.clip_end=300

def main():
    # This generator owns its Blender document, including the hidden source meshes.
    for ob in list(bpy.data.objects): bpy.data.objects.remove(ob,do_unlink=True)
    for mat in list(bpy.data.materials):
        if mat.name.startswith('M_Aether_'): bpy.data.materials.remove(mat,do_unlink=True)
    bpy.context.scene.unit_settings.system='METRIC';bpy.context.scene.unit_settings.scale_length=1
    for k in PALETTE:MATS[k]=material(k)
    wood_assets();architectural_assets();industrial_assets();liquid_assets();character_and_spell_assets()
    assemble()
    manifest={'pack':'AetherLab Prototype 01','version':1,'units':'meters','up_axis':'Z','seed':91226,
      'license':'Original assets authored for this project; no external model dependencies.',
      'asset_count':len(RECORDS),'assets':RECORDS,'placements':PLACEMENTS,
      'palette':{k:{'color':list(v[0]),'metallic':v[1],'roughness':v[2],'emissive':3 if k.startswith('Glow') else 0} for k,v in PALETTE.items()}}
    (OUT/'asset-manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'AetherLab_Prototype.blend'))
    status('complete',asset_count=len(RECORDS),placements=len(PLACEMENTS),triangles=sum(a['triangles'] for a in RECORDS))

if __name__=='__main__':
    try: main()
    except Exception:
        status('failed',error=traceback.format_exc());raise
