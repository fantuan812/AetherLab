import bpy,json,os,math,sys,hashlib
from mathutils import Vector
from pathlib import Path
import argparse
parser=argparse.ArgumentParser(description='Read-only Blender checks for the SharedCore candidate; writes only its report')
parser.add_argument('--kit-root',default=str(Path(__file__).resolve().parents[1]))
parser.add_argument('--output-report')
args=parser.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
ROOT=os.path.abspath(args.kit_root)
REPORT=args.output_report or os.path.join(ROOT,'docs/CoreKit_Validation.json')
s=bpy.context.scene
masters=bpy.data.collections['01_SOURCE_MODULES__4m_Grid'];yard=bpy.data.collections['02_LINKED_ASSEMBLY_TEST__Not_World']
tests=[]
def check(name,value,passed,details=None):
 tests.append(dict(test=name,value=value,passed=bool(passed),details=details))
def inst(asset,loc):
 arr=[o for o in yard.objects if o.get('asset_id')==asset and (o.location-Vector(loc)).length<.0001]
 if len(arr)!=1:raise RuntimeError((asset,loc,[o.name for o in arr]))
 return arr[0]
def sock(o,k):return o.matrix_world@Vector(o[k])
def join(name,a,ka,b,kb):
 delta=(sock(a,ka)-sock(b,kb)).length;check(name,delta,delta<1e-5,{'objects':[a.name,b.name],'sockets':[ka,kb],'unit':'m'})
p='KIT_Path_Flagstone_4x4_A';r='KIT_Path_Ramp_4m_Rise1m'
a=inst(p,(8,0,0));b=inst(p,(8,4,0));c=inst(p,(8,8,0));ramp=inst(r,(8,12,0));up=inst(p,(8,16,1))
join('straight seam 0→4',a,'OUT',b,'IN');join('straight seam 4→8',b,'OUT',c,'IN');join('ramp entry',c,'OUT',ramp,'IN');join('ramp exit',ramp,'OUT',up,'IN')
join('west branch',inst(p,(4,4,0)),'RIGHT',b,'LEFT');join('east branch',b,'RIGHT',inst(p,(12,4,0)),'LEFT');join('right angle route',inst(p,(12,4,0)),'OUT',inst(p,(12,8,0)),'IN')
join('low wall corner',inst('KIT_WallLow_Run_4m',(5.7,0,0)),'OUT',inst('KIT_WallLow_Run_4m',(5.7,4,0)),'IN')
join('fence straight shared post',inst('KIT_Fence_Rails_4m',(14.5,0,0)),'OUT',inst('KIT_Fence_Rails_4m',(14.5,4,0)),'IN')
join('fence right angle shared post',inst('KIT_Fence_Rails_4m',(14.5,4,0)),'OUT',inst('KIT_Fence_Rails_4m',(14.5,8,0)),'IN')
for asset,loc in [('KIT_Fence_SharedPost',(14.5,4,0)),('KIT_Fence_SharedPost',(14.5,8,0)),('KIT_WallLow_EndCornerPier',(5.7,4,0))]:
 count=sum(o.get('asset_id')==asset and (o.location-Vector(loc)).length<.0001 for o in yard.objects);check('one shared corner/support '+str(loc),count,count==1)
# Actual substrate boundary measurements (not only empty markers).
def bb(o):
 vs=[o.matrix_world@v.co for v in o.data.vertices];return [[min(v[i] for v in vs) for i in range(3)],[max(v[i] for v in vs) for i in range(3)]]
base0=inst('KIT_Path_Substrate_4x4',(8,0,0));base1=inst('KIT_Path_Substrate_4x4',(8,4,0));b0,b1=bb(base0),bb(base1)
check('substrate straight boundary gap m',b1[0][1]-b0[1][1],abs(b1[0][1]-b0[1][1])<1e-6)
check('substrate cross-boundary height gap m',b1[1][2]-b0[1][2],abs(b1[1][2]-b0[1][2])<1e-6)
rb=inst('KIT_Substrate_Ramp_4m_Rise1m',(8,12,0)); ub=inst('KIT_Path_Substrate_4x4',(8,16,1));rs=[rb.matrix_world@v.co for v in rb.data.vertices];rs_start=max(v.z for v in rs if abs(v.y-12)<1e-5);rs_end=max(v.z for v in rs if abs(v.y-16)<1e-5)
check('ramp substrate entry height gap m',rs_start-bb(base0)[1][2],abs(rs_start-bb(base0)[1][2])<1e-5)
check('ramp substrate exit height gap m',rs_end-bb(ub)[1][2],abs(rs_end-bb(ub)[1][2])<1e-5)
master_meshes={o.data for o in masters.objects if o.type=='MESH'};mesh_inst=[o for o in yard.objects if o.type=='MESH'];linked=sum(o.data in master_meshes for o in mesh_inst)
check('assembly uses shared master mesh',linked,linked==len(mesh_inst),{'mesh_instances':len(mesh_inst),'unique_meshes':len({o.data for o in mesh_inst})})
nonunit=[o.name for o in list(masters.objects)+list(yard.objects) if o.type=='MESH' and any(abs(v-1)>1e-6 for v in o.scale)];check('unit scale mesh objects',len(nonunit),not nonunit,nonunit)
invalid=[o.name for o in masters.objects if o.type=='MESH' and (not o.data.polygons or not o.data.uv_layers)];check('master geometry plus material UV',len(invalid),not invalid,invalid)
images=[dict(name=i.name,packed=bool(i.packed_file),size=list(i.size)) for i in bpy.data.images if i.source=='FILE' and i.users>0];check('referenced images packed',sum(x['packed'] for x in images),all(x['packed'] for x in images),images)
# Actual full-body bounds, armature identity and no scale adjustment, excluding review ground.
ch=bpy.data.collections['05_EXISTING_CHARACTER__UNSCALED_65_BONES'];cmeshes=[o for o in ch.objects if o.type=='MESH'];cvs=[o.matrix_world@v.co for o in cmeshes for v in o.data.vertices];z0=min(v.z for v in cvs);z1=max(v.z for v in cvs);height=z1-z0
arm=[o for o in ch.objects if o.type=='ARMATURE'];check('existing character 65 bones',len(arm[0].data.bones),len(arm)==1 and len(arm[0].data.bones)==65)
check('existing character unchanged geometric height',height,abs(height-1.8027965174987912)<1e-5,{'min_z':z0,'max_z':z1,'design_height_m':1.65,'review_material':'clay only'})
check('existing character unscaled', [list(o.scale) for o in ch.objects],all(all(abs(v-1)<1e-6 for v in o.scale) for o in ch.objects))
check('scene meters',[s.unit_settings.system,s.unit_settings.scale_length],s.unit_settings.system=='METRIC' and abs(s.unit_settings.scale_length-1)<1e-6)
# Sample the connected main substrate route by vertical rays against only substrate mesh BVHs.
from mathutils.bvhtree import BVHTree
surfaces=[o for o in yard.objects if o.get('asset_id') in ['KIT_Path_Substrate_4x4','KIT_Substrate_Ramp_4m_Rise1m']]
bvhs=[BVHTree.FromPolygons([o.matrix_world@v.co for v in o.data.vertices],[list(p.vertices) for p in o.data.polygons],all_triangles=False) for o in surfaces]
miss=[];heights=[]
for k in range(81):
 y=.001+k*19.998/80;hit=[]
 for tree in bvhs:
  q=tree.ray_cast(Vector((8,y,5)),Vector((0,0,-1)),10)
  if q[0] is not None:hit.append(q[0].z)
 if not hit:miss.append([8,y])
 else:heights.append(max(hit))
check('Blender substrate route samples',{'samples':81,'misses':len(miss)},not miss,{'misses':miss,'not_engine_navigation':True})
report=json.load(open(os.path.join(ROOT,'docs/CoreKit_Validation.json')));report.pop('tests',None);report.pop('generated_declaration_tests',None);report['substrate_bounds']=[dict(name=o.name,min=bb(o)[0],max=bb(o)[1],space='world_meters_after_reopen') for o in surfaces];report['reopen_blender_version']=list(bpy.app.version);report['measured_tests']=tests;report['all_measured_checks_pass']=all(t['passed'] for t in tests);report['validation_status']='reopened_checks_complete';report['verification_date_utc']='2026-10-01';report['character_height_design_m']=1.65;report['character_height_actual_baseline_m']=height;report['character_scale_modified']=False;report['actual_character_material_override']='clay review only; source asset untouched';report['check_breakdown']={'socket_coincidence':10,'shared_support_count':3,'substrate_boundary_and_height':4,'data_character_packing_and_units':8,'centerline_81_samples':1};report['passed_scope']='Blender anchor positions, 4 measured substrate boundaries/heights, shared-data/packing/character checks and 81 main-route centerline samples only. Not UV quality, normal quality, LOD, whole-surface seam, capsule clearance, collision, GPU performance or engine navigation acceptance';report['source_inputs']={'scene_library_file_id':'libfile_7bdc97dcea808191af98e31eaff40bab','scene_version':7,'reference_library_file_id':'libfile_5be039fa03d88191b06649909169f1fa','character_local_source':'CHR_Peasant_Original65_Baseline.blend'}
json.dump(report,open(REPORT,'w'),ensure_ascii=False,indent=2)
print(json.dumps({'tests':len(tests),'passed':sum(t['passed'] for t in tests),'failed':[t for t in tests if not t['passed']],'assembly_meshes':len(mesh_inst),'assembly_unique_meshes':len({o.data for o in mesh_inst}),'images':images,'character_height_m':height},ensure_ascii=False))
if not report['all_measured_checks_pass']:raise SystemExit(1)
