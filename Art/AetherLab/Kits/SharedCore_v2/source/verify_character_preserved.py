import bpy,hashlib,json,os,sys,argparse
from pathlib import Path
parser=argparse.ArgumentParser(description='Compare the kit review character against the unmodified approved baseline')
parser.add_argument('--character-source',required=True)
parser.add_argument('--output-report',default=str(Path(__file__).resolve().parents[1]/'docs/Character_Preservation_Check.json'))
args=parser.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
def sig(o):
 if o.type=='ARMATURE':return {'type':o.type,'bones':len(o.data.bones),'rest':hashlib.sha256(repr([(b.name,tuple(round(v,7) for row in b.matrix_local for v in row),b.parent.name if b.parent else None) for b in o.data.bones]).encode()).hexdigest()}
 if o.type=='MESH':return {'type':o.type,'vertices':len(o.data.vertices),'geometry':hashlib.sha256(repr([(tuple(v.co),[(g.group,g.weight) for g in v.groups]) for v in o.data.vertices]).encode()).hexdigest(),'topology':hashlib.sha256(repr([tuple(p.vertices) for p in o.data.polygons]).encode()).hexdigest(),'groups':[(g.index,g.name) for g in o.vertex_groups]}
col=bpy.data.collections['05_EXISTING_CHARACTER__UNSCALED_65_BONES'];outs={o.name:sig(o) for o in col.objects if o.type in ['MESH','ARMATURE']};names=list(outs)
with bpy.data.libraries.load(os.path.abspath(args.character_source),link=False) as (src,dst):dst.objects=list(names)
results=[]
for name,ob in zip(names,dst.objects):results.append({'object':name,'unchanged_geometry_weights_or_rest':outs[name]==sig(ob),'source':sig(ob),'kit':outs[name]})
rep={'all_passed':all(r['unchanged_geometry_weights_or_rest'] for r in results),'bone_count':65,'scope':'Geometry, mesh topology, weights, group names and armature rest compared to the approved baseline; material override intentionally excluded; translation only','source_library_file_id':'libfile_d49623233fe88191b50a13860ee7cb46','results':results}
json.dump(rep,open(args.output_report,'w'),ensure_ascii=False,indent=2);print('CHARACTER_PRESERVED',rep['all_passed'],len(results))

if not rep["all_passed"]:raise SystemExit(1)
