#include "Misc/AutomationTest.h"
#include "Inventory/AetherInventoryState.h"
#include "Inventory/AetherInventoryCodec.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
using E=EAetherInventoryMutationCode;
FGuid Id(int32 N){return FGuid(0xA37E0010,0,0,N);}
FAetherV10ItemDefinitions Definitions(FString& Reason)
{
    FString Json;FFileHelper::LoadFileToString(Json,*(FPaths::ProjectContentDir()/TEXT("AetherCore/Definitions/V10/Items.json")));
    return FAetherV10ItemDefinitions::Parse(Json,Reason);
}
FAetherV10ItemInstance Item(int32 Number,const TCHAR* Definition,int32 Cell,const FAetherV10ItemDefinitions& D,int32 Quantity=1)
{
    FAetherV10ItemInstance I;I.InstanceId=Id(Number);I.DefinitionId=Definition;I.SlotIndex=Cell;I.Quantity=Quantity;
    const auto* Def=D.Items.Find(Definition);I.Durability=Def&&Def->MaxDurability>0?Def->MaxDurability:-1;return I;
}
int32 Count(const FAetherInventoryStateV10& S,const FString& Definition)
{int32 N=0;for(const auto& I:S.Items)if(I.DefinitionId==Definition)N+=I.Quantity;return N;}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherGridTest,"Aether.V10.Inventory.FixedGridIdentityAndAtomicity",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherGridTest::RunTest(const FString&)
{
    FString Reason;const auto D=Definitions(Reason);
    if(!TestTrue(*Reason,D.Validate(Reason)))return false;
    FAetherInventoryStateV10 S;S.Items.Add(Item(1,TEXT("Potion"),0,D,15));S.Items.Add(Item(2,TEXT("Potion"),5,D,18));
    TestTrue(TEXT("Sparse grid is valid"),S.Validate(D,Reason));
    TestEqual(TEXT("Empty cell is not filtered view index"),S.FirstEmpty(),1);
    TestTrue(TEXT("Move preserves instance ID"),S.Move(Id(1),7,D).Code==E::Applied&&S.At(7)->InstanceId==Id(1)&&!S.At(0));
    TestTrue(TEXT("Occupied target requires explicit swap"),S.Move(Id(1),5,D).Code==E::Occupied&&S.Find(Id(1))->SlotIndex==7);
    TestTrue(TEXT("Swap atomically changes only positions"),S.Swap(Id(1),Id(2),D).Code==E::Applied&&S.At(5)->InstanceId==Id(1)&&S.At(7)->InstanceId==Id(2));
    const auto Split=S.Split(Id(1),5,Id(3),10,D);
    TestTrue(TEXT("Split creates a new identity at requested cell"),Split.Code==E::Applied&&S.Find(Id(1))->Quantity==10&&S.Find(Id(3))->Quantity==5&&S.At(10)->InstanceId==Id(3));
    TestEqual(TEXT("Split preserves total"),Count(S,TEXT("Potion")),33);
    TestTrue(TEXT("Split reports identity transition"),Split.Transitions.Num()==1&&Split.Transitions[0].From==Id(1)&&Split.Transitions[0].To==Id(3));
    const auto Merge=S.Merge(Id(3),Id(2),5,D);
    TestTrue(TEXT("Merge transfers only available capacity"),Merge.Code==E::Applied&&Merge.ActualQuantity==2&&S.Find(Id(2))->Quantity==20&&S.Find(Id(3))->Quantity==3);
    TestTrue(TEXT("Duplicate split ID rejected without source loss"),S.Split(Id(1),1,Id(2),-1,D).Code==E::Invalid&&S.Find(Id(1))->Quantity==10);
    TestTrue(TEXT("Overflow quantity rejected without mutation"),S.Merge(Id(1),Id(3),MAX_int32,D).Code==E::Invalid&&Count(S,TEXT("Potion"))==33);
    TestTrue(TEXT("Compaction is explicit"),S.Sort(true,D).Code==E::Applied&&Count(S,TEXT("Potion"))==33&&S.Items.Num()==2&&S.At(0)&&S.At(1)&&!S.At(2));
    const auto First=S.Items[0].InstanceId;const auto Second=S.Items[1].InstanceId;
    TestTrue(TEXT("Repeated sort is deterministic"),S.Sort(true,D).Code==E::Applied&&S.Items[0].InstanceId==First&&S.Items[1].InstanceId==Second);
    FAetherInventoryStateV10 Consumed;
    Consumed.Items.Add(Item(1,TEXT("Potion"),0,D,17));Consumed.Items.Add(Item(2,TEXT("Potion"),1,D,3));Consumed.Items.Add(Item(3,TEXT("Potion"),2,D,5));
    TestTrue(TEXT("Sorting cannot resurrect an exhausted source ID"),Consumed.Sort(true,D).Code==E::Applied&&!Consumed.Find(Id(2))&&Consumed.Find(Id(3))->Quantity==5);
    auto FakeCapacity=S;FakeCapacity.Capacity=64;
    TestFalse(TEXT("UI cannot manufacture capacity"),FakeCapacity.Validate(D,Reason));
    S.Items[1].SlotIndex=S.Items[0].SlotIndex;
    TestFalse(TEXT("Duplicate cell fails invariant"),S.Validate(D,Reason));
    TestTrue(TEXT("Operations cannot repair corrupt input silently"),S.Move(First,4,D).Code==E::Invalid);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherStackStateTest,"Aether.V10.Inventory.StackKeyAndRemovalPolicy",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherStackStateTest::RunTest(const FString&)
{
    FString Reason;const auto D=Definitions(Reason);if(!TestTrue(*Reason,D.Validate(Reason)))return false;
    FAetherInventoryStateV10 Base;Base.Items.Add(Item(1,TEXT("Potion"),0,D,3));Base.Items.Add(Item(2,TEXT("Potion"),1,D,4));
    auto RejectChange=[&](TFunction<void(FAetherV10ItemInstance&)> Change,const TCHAR* Label)
    {
        auto S=Base;Change(S.Items[1]);
        TestFalse(Label,S.Items[0].SameStackKey(S.Items[1]));
        TestTrue(TEXT("Merge preserves incompatible states and quantities"),S.Merge(Id(1),Id(2),2,D).Code!=E::Applied&&S.Items[0].Quantity==3&&S.Items[1].Quantity==4);
    };
    RejectChange([](auto& I){I.Quality=1;},TEXT("Quality belongs to stack identity"));
    RejectChange([](auto& I){I.Affixes.Add(TEXT("Roll"),3);},TEXT("Affix rolls belong to stack identity"));
    RejectChange([](auto& I){I.BoundToCharacter=TEXT("Other");},TEXT("Binding belongs to stack identity"));
    RejectChange([](auto& I){I.QuestInstanceId=Id(99);},TEXT("Quest instance belongs to stack identity"));
    RejectChange([](auto& I){I.StateGroup=TEXT("Wet");},TEXT("Storage state belongs to stack identity"));
    RejectChange([](auto& I){I.Durability=0;},TEXT("Durability cannot be washed away"));
    RejectChange([](auto& I){I.bFavorite=true;},TEXT("Favorite state is retained"));
    RejectChange([](auto& I){I.bLocked=true;},TEXT("Lock state is retained"));
    auto S=Base;
    TestTrue(TEXT("Favorite is distinct from lock"),S.SetFavorite(Id(1),true,D).Code==E::Applied&&S.Find(Id(1))->bFavorite&&!S.Find(Id(1))->bLocked&&S.CanRemove(Id(1),TEXT("Owner"),true,D)==E::Applied);
    TestTrue(TEXT("User locks block accidental sale"),S.SetLocked(Id(1),true,D).Code==E::Applied&&S.CanRemove(Id(1),TEXT("Owner"),true,D)==E::Locked);
    TestTrue(TEXT("Locks also block dropping"),S.CanRemove(Id(1),TEXT("Owner"),false,D)==E::Locked);
    S.SetLocked(Id(1),false,D);S.Items[0].BoundToCharacter=TEXT("Owner");
    TestTrue(TEXT("Owner cannot sell away binding"),S.CanRemove(Id(1),TEXT("Owner"),true,D)==E::Bound);
    S=Base;S.Items[0].Affixes.Add(TEXT("A"),1);S.Items[0].Affixes.Add(TEXT("B"),2);
    S.Items[1].Affixes.Add(TEXT("B"),2);S.Items[1].Affixes.Add(TEXT("A"),1);
    TestTrue(TEXT("Affix map order does not prevent legal stacking"),S.Merge(Id(1),Id(2),3,D).Code==E::Applied&&S.Items.Num()==1&&S.Items[0].InstanceId==Id(2));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherEquipmentStateTest,"Aether.V10.Inventory.TenSlotsFullBagAndDurability",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherEquipmentStateTest::RunTest(const FString&)
{
    FString Reason;const auto D=Definitions(Reason);if(!TestTrue(*Reason,D.Validate(Reason)))return false;
    const TCHAR* Names[]={TEXT("TrainingSword"),TEXT("TrainingShield"),TEXT("LeatherCap"),TEXT("LeatherVest"),TEXT("LeatherGloves"),
        TEXT("LeatherLeggings"),TEXT("LeatherBoots"),TEXT("CopperNecklace"),TEXT("CopperRing"),TEXT("CopperRing")};
    const TCHAR* Slots[]={TEXT("MainHand"),TEXT("OffHand"),TEXT("Head"),TEXT("Chest"),TEXT("Hands"),
        TEXT("Legs"),TEXT("Feet"),TEXT("Neck"),TEXT("Ring1"),TEXT("Ring2")};
    FAetherInventoryStateV10 S;
    for(int32 N=0;N<10;++N)S.Items.Add(Item(N+1,Names[N],N,D));
    S.Items.Add(Item(11,TEXT("TrainingHammer"),10,D));
    for(int32 N=11;N<32;++N)S.Items.Add(Item(N+1,N==31?TEXT("Potion"):TEXT("TrainingSword"),N,D,N==31?10:1));
    for(int32 N=0;N<10;++N)TestTrue(TEXT("Every declared slot can hold a legal item"),S.Equip(Id(N+1),Slots[N],TEXT("Owner"),D).Code==E::Applied);
    TestEqual(TEXT("All ten slots coexist"),S.Equipment.Num(),10);
    TestEqual(TEXT("Equipment still occupies inventory capacity"),S.Items.Num(),32);
    TestTrue(TEXT("Full bag can unequip"),S.Unequip(Id(1),D).Code==E::Applied&&S.Items.Num()==32);
    TestTrue(TEXT("Two-handed equip atomically removes offhand reference"),S.Equip(Id(11),TEXT("MainHand"),TEXT("Owner"),D).Code==E::Applied&&!S.Equipment.Contains(TEXT("OffHand")));
    TestTrue(TEXT("Cannot silently replace two-handed grip with shield"),S.Equip(Id(2),TEXT("OffHand"),TEXT("Owner"),D).Code==E::Occupied&&S.Equipment.FindRef(TEXT("MainHand"))==Id(11));
    TestTrue(TEXT("Same ring moves between allowed slots without cloning"),S.Equip(Id(9),TEXT("Ring2"),TEXT("Owner"),D).Code==E::Applied&&!S.Equipment.Contains(TEXT("Ring1"))&&S.Equipment.FindRef(TEXT("Ring2"))==Id(9));
    TestTrue(TEXT("Full bag cannot split"),S.Split(Id(32),1,Id(99),-1,D).Code==E::Capacity&&S.Items.Num()==32&&S.Find(Id(32))->Quantity==10);
    FAetherInventoryStateV10 Wear;Wear.Items.Add(Item(1,TEXT("LeatherVest"),0,D));
    Wear.Equip(Id(1),TEXT("Chest"),TEXT("Owner"),D);
    TestEqual(TEXT("Intact armor contributes defined statistic"),Wear.EquippedStats(D).FindRef(TEXT("Armor")),5.);
    TestTrue(TEXT("Wear saturates at broken"),Wear.Wear(Id(1),MAX_int32,D).Code==E::Applied&&Wear.Find(Id(1))->Durability==0);
    TestEqual(TEXT("Broken modifier changes calculated statistic"),Wear.EquippedStats(D).FindRef(TEXT("Armor")),1.25);
    TestTrue(TEXT("Repair restores candidate durability"),Wear.Repair(Id(1),D).Code==E::Applied&&Wear.Find(Id(1))->Durability==100);
    TestEqual(TEXT("Repeated stat query cannot accumulate effects"),Wear.EquippedStats(D).FindRef(TEXT("Armor")),5.);
    TestTrue(TEXT("Disabled durability has no fake wear"),S.Wear(Id(1),1,D).Code==E::NotAllowed);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherItemDefinitionTest,"Aether.V10.Inventory.DefinitionValidation",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherItemDefinitionTest::RunTest(const FString&)
{
    FString Reason,Json;
    FFileHelper::LoadFileToString(Json,*(FPaths::ProjectContentDir()/TEXT("AetherCore/Definitions/V10/Items.json")));
    const auto Valid=FAetherV10ItemDefinitions::Parse(Json,Reason);
    if(!TestTrue(*Reason,Valid.Validate(Reason)))return false;
    auto D=Valid;D.ContentSchemaVersion=999;TestFalse(TEXT("Unknown content schema rejected"),D.Validate(Reason));
    D=Valid;D.Slots.Pop();TestFalse(TEXT("Ten-slot layout cannot silently lose a slot"),D.Validate(Reason));
    D=Valid;D.Items.FindChecked(TEXT("Potion")).SellPrice=100;TestFalse(TEXT("Economy arbitrage rejected"),D.Validate(Reason));
    D=Valid;D.Items.FindChecked(TEXT("LeatherVest")).IconId.Reset();TestFalse(TEXT("Missing icon identity rejected"),D.Validate(Reason));
    D=Valid;D.Items.FindChecked(TEXT("LeatherVest")).Stats.Add(TEXT("PretendAttribute"),10);TestFalse(TEXT("Unknown stat row rejected"),D.Validate(Reason));
    D=Valid;D.Items.FindChecked(TEXT("CopperRing")).AllowedSlots.Add(TEXT("Ring3"));TestFalse(TEXT("Unknown allowed slot rejected"),D.Validate(Reason));
    D=Valid;D.Items.FindChecked(TEXT("TrainingSword")).MaxStack=2;TestFalse(TEXT("Equipment cannot become stackable"),D.Validate(Reason));
    FString Duplicate=Json.Replace(TEXT("\"Id\": \"Material\""),TEXT("\"Id\": \"Potion\""));
    auto Parsed=FAetherV10ItemDefinitions::Parse(Duplicate,Reason);
    TestFalse(TEXT("Duplicate item IDs rejected instead of last-write-wins"),Parsed.Validate(Reason));
    TestTrue(TEXT("Ring declares both legal slots"),Valid.Items.FindChecked(TEXT("CopperRing")).AllowedSlots.Num()==2);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherInventoryCodecTest,"Aether.V10.Inventory.VersionedBinarySnapshot",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherInventoryCodecTest::RunTest(const FString&)
{
    FString Reason;const auto D=Definitions(Reason);if(!TestTrue(*Reason,D.Validate(Reason)))return false;
    FAetherInventoryStateV10 S;S.Items.Add(Item(1,TEXT("LeatherVest"),7,D));S.Items.Add(Item(2,TEXT("CopperRing"),19,D));
    S.Items[0].Durability=13;S.Items[0].Quality=2;S.Items[0].BoundToCharacter=TEXT("测试角色");
    S.Items[0].Affixes.Add(TEXT("B"),42);S.Items[0].Affixes.Add(TEXT("A"),3);
    S.Items[0].StateGroup=TEXT("Wet");S.Items[0].QuestInstanceId=Id(99);S.Items[0].bLocked=true;S.Items[0].bFavorite=true;
    S.Equip(Id(1),TEXT("Chest"),TEXT("测试角色"),D);S.Equip(Id(2),TEXT("Ring2"),TEXT("测试角色"),D);
    TArray<uint8> Bytes,Again;FAetherInventoryStateV10 Restored;
    if(!TestTrue(TEXT("Explicit DTO encodes"),AetherInventoryCodec::Encode(S,D,Bytes,Reason)))return false;
    if(!TestTrue(*Reason,AetherInventoryCodec::Decode(Bytes,D,Restored,Reason)))return false;
    TestTrue(TEXT("All instance state survives decoding"),Restored.Find(Id(1))->SameStackKey(*S.Find(Id(1))));
    TestEqual(TEXT("Sparse slot position retained"),Restored.Find(Id(1))->SlotIndex,7);
    TestTrue(TEXT("Equipment still references the original GUID"),Restored.Equipment.OrderIndependentCompareEqual(S.Equipment));
    auto Reordered=S;Reordered.Items.Swap(0,1);Reordered.Equipment.Reset();
    Reordered.Equipment.Add(TEXT("Ring2"),Id(2));Reordered.Equipment.Add(TEXT("Chest"),Id(1));
    Reordered.Items[1].Affixes.Reset();Reordered.Items[1].Affixes.Add(TEXT("A"),3);Reordered.Items[1].Affixes.Add(TEXT("B"),42);
    TestTrue(TEXT("Container insertion order is not part of persistent identity"),AetherInventoryCodec::Encode(Reordered,D,Again,Reason)&&Bytes==Again);
    for(int32 N=0;N<Bytes.Num();++N)
    {
        TArray<uint8> Truncated;Truncated.Append(Bytes.GetData(),N);
        TestFalse(TEXT("Every truncated persisted DTO rejected"),AetherInventoryCodec::Decode(Truncated,D,Restored,Reason));
        TestTrue(TEXT("Failure preserves caller state"),Restored.Items.Num()==2&&Restored.Find(Id(1))&&Restored.Find(Id(1))->Durability==13);
    }
    Again=Bytes;Again[4]=99;TestFalse(TEXT("Unknown save DTO schema rejected"),AetherInventoryCodec::Decode(Again,D,Restored,Reason));
    Again=Bytes;Again[6]=99;TestFalse(TEXT("Unknown content schema rejected"),AetherInventoryCodec::Decode(Again,D,Restored,Reason));
    Again=Bytes;Again[10]=255;Again[11]=255;TestFalse(TEXT("Forged count rejected before allocation"),AetherInventoryCodec::Decode(Again,D,Restored,Reason));
    Again=Bytes;Again.Add(0);TestFalse(TEXT("Trailing storage bytes rejected"),AetherInventoryCodec::Decode(Again,D,Restored,Reason));
    TestTrue(TEXT("No rejected record became an empty replacement"),Restored.Find(Id(1))&&Restored.Equipment.Num()==2);
    return true;
}
#endif
