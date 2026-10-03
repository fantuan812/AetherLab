"""Reopen an extracted portable source and verify geometry plus real image bytes."""
import bpy,pathlib,json,hashlib,struct,argparse,sys
p=argparse.ArgumentParser();p.add_argument('--manifest',required=True);p.add_argument('--output',required=True);a=p.parse_args(sys.argv[sys.argv.index('--')+1:]);d=json.loads(pathlib.Path(a.manifest).read_text());bpy.context.view_layer.update();h=hashlib.sha256()
for me in sorted(bpy.data.meshes,key=lambda x:x.name):
 h.update(me.name.encode());h.update(struct.pack('<II',len(me.vertices),len(me.polygons)))
 for v in me.vertices:h.update(struct.pack('<3f',*v.co))
 for f in me.polygons:h.update(struct.pack('<I',len(f.vertices)));h.update(struct.pack('<'+'I'*len(f.vertices),*f.vertices));h.update(struct.pack('<I',f.material_index))
 for uv in me.uv_layers:
  h.update(uv.name.encode())
  for x in uv.data:h.update(struct.pack('<2f',*x.uv))
for o in sorted(bpy.data.objects,key=lambda x:x.name):h.update(o.name.encode());h.update(o.type.encode());h.update(struct.pack('<16f',*[v for row in o.matrix_world for v in row]))
checks=[]
for row in d['image_datablocks']:
 im=bpy.data.images[row['image_id']];path=pathlib.Path(bpy.path.abspath(im.filepath));im.reload();pixel_probe=im.pixels[0] if len(im.pixels) else None;ok=path.is_file() and hashlib.sha256(path.read_bytes()).hexdigest()==row['sha256'] and im.has_data and list(im.size)==row['size_px'] and im.colorspace_settings.name==row['color_space'] and not im.packed_file;checks.append({'image_id':im.name,'relative_path':im.filepath,'exists':path.is_file(),'decoded':im.has_data,'size_px':list(im.size),'color_space':im.colorspace_settings.name,'unpacked':not bool(im.packed_file),'passed':ok})
passed=h.hexdigest()==d['geometry_objects_uv_digest'] and all(x['passed'] for x in checks)
out={'portable_source_sha256':hashlib.sha256(pathlib.Path(bpy.data.filepath).read_bytes()).hexdigest(),'geometry_objects_uv_digest':h.hexdigest(),'matches_frozen_geometry':h.hexdigest()==d['geometry_objects_uv_digest'],'images':checks,'passed':passed,'scope':'Portable file read on a fresh Blender process; external image bytes, dimensions, color spaces and actual geometry/object transforms/UV. No runtime/UE claim.'}
pathlib.Path(a.output).write_text(json.dumps(out,ensure_ascii=False,indent=2));print('PORTABLE_VERIFIED',passed,flush=True)
if not passed:raise RuntimeError('Portable copy validation failed')
