"""Independent geometry and export inventory checks on the saved deliverable."""
import bpy
import bmesh
import math
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
ART=ROOT/'Art/AetherLab'
bpy.ops.wm.open_mainfile(filepath=str(ART/'AetherLab_Prototype.blend'))
manifest=json.loads((ART/'asset-manifest.json').read_text(encoding='utf-8'))
report={'asset_count':len(manifest['assets']),'errors':[],'assets':[]}
for entry in manifest['assets']:
    ob=bpy.data.objects.get(entry['name'])
    errors=[]
    if not ob or ob.type!='MESH':
        report['errors'].append(entry['name']+': missing mesh');continue
    if any(not math.isfinite(c) for v in ob.data.vertices for c in v.co): errors.append('non-finite vertex')
    if len(ob.data.uv_layers)<2:errors.append('missing UV0 or packed lightmap UV1')
    if any(abs(c)>1e-6 for c in ob.location):errors.append('source pivot displaced')
    if any(abs(c-1)>1e-6 for c in ob.scale):errors.append('unapplied scale')
    if any(m is None for m in ob.data.materials):errors.append('empty material slot')
    bm=bmesh.new();bm.from_mesh(ob.data)
    nonmanifold=sum(not e.is_manifold for e in bm.edges)
    if nonmanifold:errors.append(f'{nonmanifold} non-manifold edges')
    bm.free()
    ob.data.calc_loop_triangles()
    degenerate=sum(t.area<1e-12 for t in ob.data.loop_triangles)
    if degenerate:errors.append(f'{degenerate} degenerate triangles')
    for fmt in ('fbx','glb'):
        path=ART/entry[fmt]
        if not path.is_file() or path.stat().st_size<1000:errors.append('missing or empty '+fmt)
    report['assets'].append({'name':entry['name'],'triangles':len(ob.data.loop_triangles),
        'uv_layers':len(ob.data.uv_layers),'non_manifold_edges':nonmanifold,'errors':errors})
    report['errors'].extend(entry['name']+': '+e for e in errors)
report['passed']=not report['errors']
(ART/'geometry-validation.json').write_text(json.dumps(report,indent=2))
print('AETHER_GEOMETRY_VALIDATION',json.dumps({'passed':report['passed'],'asset_count':report['asset_count'],'errors':report['errors']}))
if report['errors']:raise RuntimeError('Geometry validation failed')
