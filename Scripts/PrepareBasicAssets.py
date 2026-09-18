"""Run with UE 5.8 PythonScriptPlugin after building the native classes."""
import unreal as ue
L=ue.EditorAssetLibrary
T=ue.AssetToolsHelpers.get_asset_tools()
def create(name,cls):
    path='/Game/AetherCore/Data/'+name
    if L.does_asset_exist(path):return L.load_asset(path)
    f=ue.DataAssetFactory();f.set_editor_property('data_asset_class',cls)
    return T.create_asset(name,'/Game/AetherCore/Data',cls,f)
def props(obj,**kw):
    for k,v in kw.items():obj.set_editor_property(k,v)
    return obj
def slot(name,item):
    s=ue.AetherEquippedSlot();s.slot=name;s.item_id=item;return s
def attack(name,damage,cost,reach,impulse,wind):
    a=ue.AetherAttackDefinition();props(a,id=name,damage=damage,stamina_cost=cost,reach_cm=reach,impulse_ns=impulse,windup_seconds=wind,active_seconds=.18,recovery_seconds=.3,posture_damage=damage);return a
cube=L.load_asset('/Engine/BasicShapes/Cube');cylinder=L.load_asset('/Engine/BasicShapes/Cylinder')
items=[]
for name,scale,two,off in [('TrainingSword',(.07,.1,.9),False,False),('TrainingHammer',(.25,.25,.95),True,False),('TideStaff',(.07,.07,1.6),True,False),('TrainingShield',(.45,.45,.06),False,True),('BellHammer',(.3,.3,1.25),True,False),('EmberFocus',(.18,.18,.7),False,False)]:
    item=create('DA_'+name,ue.AetherEquipmentDefinition)
    props(item,item_id=name,display_name=name,slot='OffHand' if off else 'MainHand',socket='hand_l' if off else 'hand_r',mesh=cylinder if off else cube,occupies_both_hands=two,allows_guard=off,grip_transform=ue.Transform(location=ue.Vector(0,0,30),rotation=ue.Rotator(0,0,0),scale=ue.Vector(*scale)))
    item.set_editor_property('attacks',[] if off else [attack('Light',26 if two else 16,16 if two else 8,210 if two else 165,18 if two else 6,.2 if two else .08),attack('Heavy',45 if two else 32,30 if two else 24,230 if two else 180,50 if two else 32,.3)])
    if name=='TrainingSword':
        attacks=list(item.get_editor_property('attacks'))
        for a in attacks:a.set_editor_property('cutting_work_j',24.0 if str(a.id)=='Light' else 40.0)
        item.set_editor_property('attacks',attacks)
    L.save_loaded_asset(item);items.append(item)
cat=create('DA_EquipmentCatalog',ue.AetherEquipmentCatalog);cat.set_editor_property('items',items);L.save_loaded_asset(cat)
body=L.load_asset('/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple')
walk=L.load_asset('/Game/Characters/Mannequins/Anims/Unarmed/Jog/MF_Unarmed_Jog_Fwd')
hit=L.load_asset('/Game/Characters/Mannequins/Anims/Unarmed/Attack/MM_Attack_01')
assert body and walk and hit,'Official template assets unavailable'
chars={}
for name in ['Player','Guard','Caster','Boss']:
    c=create('DA_Character_'+name,ue.AetherCharacterDefinition)
    eq=[slot('MainHand','BellHammer')] if name=='Boss' else [slot('MainHand','EmberFocus')] if name=='Caster' else [slot('MainHand','TrainingSword'),slot('OffHand','TrainingShield')]
    props(c,character_id='UE_'+name,body_mesh=body,walk_animation=walk,attack_animation=hit,capsule_half_height=88,capsule_radius=34,initial_equipment=eq,quick_equip_items=['TrainingSword','TrainingHammer','TideStaff'])
    L.save_loaded_asset(c);chars[name]=c
content=create('DA_GameContent',ue.AetherGameContent);props(content,equipment_catalog=cat,player=chars['Player'],guard=chars['Guard'],caster=chars['Caster'],boss=chars['Boss']);L.save_loaded_asset(content)
print('AETHER_BASIC_ASSETS_PASS')
