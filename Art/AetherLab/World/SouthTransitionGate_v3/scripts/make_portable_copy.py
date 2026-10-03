"""Write a separate external-texture delivery copy. Never overwrite the frozen source."""
import bpy,os,json,argparse,sys,hashlib,struct,pathlib
p=argparse.ArgumentParser();p.add_argument('--output',required=True);p.add_argument('--expected-source-sha',required=True);a=p.parse_args(sys.argv[sys.argv.index('--')+1:]);R=pathlib.Path(a.output).resolve();(R/'source/textures').mkdir(parents=True,exist_ok=True);(R/'docs').mkdir(exist_ok=True)
original=pathlib.Path(bpy.data.filepath);before=hashlib.sha256(original.read_bytes()).hexdigest()
if before!=a.expected_source_sha:raise RuntimeError('Source SHA mismatch')
bpy.context.view_layer.update()
def geo_hash():
 h=hashlib.sha256()
 for me in sorted(bpy.data.meshes,key=lambda x:x.name):
  h.update(me.name.encode());h.update(struct.pack('<II',len(me.vertices),len(me.polygons)))
  for v in me.vertices:h.update(struct.pack('<3f',*v.co))
  for f in me.polygons:h.update(struct.pack('<I',len(f.vertices)));h.update(struct.pack('<'+'I'*len(f.vertices),*f.vertices));h.update(struct.pack('<I',f.material_index))
  for uv in me.uv_layers:
   h.update(uv.name.encode())
   for x in uv.data:h.update(struct.pack('<2f',*x.uv))
 for o in sorted(bpy.data.objects,key=lambda x:x.name):
  h.update(o.name.encode());h.update(o.type.encode());h.update(struct.pack('<16f',*[v for row in o.matrix_world for v in row]))
 return h.hexdigest()
geom=geo_hash();rows=[];byhash={};bpy.data.use_autopack=False
for im in bpy.data.images:
 if not im.packed_file:raise RuntimeError('Unexpected unpacked image: '+im.name)
 raw=bytes(im.packed_file.data);sha=hashlib.sha256(raw).hexdigest();size=list(im.size);space=im.colorspace_settings.name
 if sha not in byhash:
  name='Texture_'+sha[:16]+'.png';(R/'source/textures'/name).write_bytes(raw);byhash[sha]=name
 name=byhash[sha];absolute=str(R/'source/textures'/name)
 for packed in im.packed_files:packed.filepath=absolute
 im.unpack(method='USE_ORIGINAL');im.filepath_raw=absolute;im.reload()
 if len(im.pixels):pixel_probe=im.pixels[0]
 print('EXTERNAL_IMAGE',im.name,im.filepath,list(im.size),im.has_data,flush=True)
 if not im.has_data or list(im.size)!=size:raise RuntimeError('Image did not reload: '+im.name)
 im.filepath_raw='//textures/'+name
 rows.append({'image_id':im.name,'relative_path':'source/textures/'+name,'bytes':len(raw),'sha256':sha,'size_px':size,'color_space':space})
if geo_hash()!=geom:raise RuntimeError('Geometry changed during externalization')
out=R/'source/AetherLab_Global_World_Blockout_v1.blend'
bpy.ops.wm.save_as_mainfile(filepath=str(out),compress=True,relative_remap=False)
if hashlib.sha256(original.read_bytes()).hexdigest()!=before:raise RuntimeError('Frozen source unexpectedly changed')
receipt={'frozen_source_file':original.name,'frozen_source_sha256':before,'portable_file':'source/'+out.name,'portable_sha256':hashlib.sha256(out.read_bytes()).hexdigest(),'portable_bytes':out.stat().st_size,'geometry_objects_uv_digest':geom,'image_datablocks':rows,'unique_texture_files':len(byhash),'reason':'Different blend bytes are expected: only image packing/path changes, no image re-encoding, original frozen file unchanged','reopen_verified':False}
(R/'docs/Portable_Manifest.json').write_text(json.dumps(receipt,ensure_ascii=False,indent=2));print('PORTABLE',json.dumps({'bytes':out.stat().st_size,'textures':len(byhash),'sha256':receipt['portable_sha256']}),flush=True)
