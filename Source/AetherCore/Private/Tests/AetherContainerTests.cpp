#include "Misc/AutomationTest.h"
#include "World/AetherContainerCodec.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherContainerTransferTest,"Aether.V10.Inventory.AtomicCrossContainerIdentity",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherContainerTransferTest::RunTest(const FString&)
{
    FString Json,Reason;FFileHelper::LoadFileToString(Json,*(FPaths::ProjectContentDir()/TEXT("AetherCore/Definitions/V10/Items.json")));
    const auto D=FAetherV10ItemDefinitions::Parse(Json,Reason);
    FAetherInventoryStateV10 From,To;To.Capacity=2;
    FAetherV10ItemInstance Item;Item.InstanceId=FGuid(1,2,3,4);Item.DefinitionId=TEXT("Potion");Item.SlotIndex=0;Item.Quantity=10;
    Item.Quality=2;Item.Affixes.Add(TEXT("Test.Affix"),7);Item.BoundToCharacter=TEXT("Alice");Item.QuestInstanceId=FGuid(5,6,7,8);Item.StateGroup=TEXT("Wet");Item.bFavorite=true;
    From.Items.Add(Item);auto Result=From.TransferTo(To,Item.InstanceId,10,false,D);
    TestTrue(TEXT("Whole instance preserves GUID and exact stack state"),Result.Code==EAetherInventoryMutationCode::Applied&&From.Items.IsEmpty()&&To.Items.Num()==1&&To.Items[0].InstanceId==Item.InstanceId&&To.Items[0].SameStackKey(Item));
    TestTrue(TEXT("Whole transfer records identity relation"),Result.Transitions.Num()==1&&Result.Transitions[0].From==Item.InstanceId&&Result.Transitions[0].To==Item.InstanceId&&Result.Transitions[0].Quantity==10);
    Result=To.TransferTo(From,Item.InstanceId,3,false,D);
    TestTrue(TEXT("Partial transfer leaves source and creates explicit split identity"),Result.ActualQuantity==3&&To.Items[0].Quantity==7&&From.Items.Num()==1&&From.Items[0].InstanceId!=Item.InstanceId&&From.Items[0].SameStackKey(Item)&&Result.Transitions[0].To==From.Items[0].InstanceId);
    // 两个满格但未满堆的目标允许只拾取两件，源余量保持原 GUID。
    From={};To={};To.Capacity=2;Item.Quality=0;Item.Affixes.Reset();Item.BoundToCharacter.Reset();Item.QuestInstanceId={};Item.StateGroup.Reset();Item.bFavorite=false;From.Items.Add(Item);
    for(int32 I=0;I<2;++I){auto Target=Item;Target.InstanceId=FGuid(9,8,7,I+1);Target.SlotIndex=I;Target.Quantity=19;To.Items.Add(Target);}
    TestTrue(TEXT("All-or-nothing capacity failure changes neither side"),From.TransferTo(To,Item.InstanceId,10,false,D).Code==EAetherInventoryMutationCode::Capacity&&From.Items[0].Quantity==10&&To.Items[0].Quantity==19);
    Result=From.TransferTo(To,Item.InstanceId,10,true,D);
    TestTrue(TEXT("Partial pickup transfers only available capacity"),Result.ActualQuantity==2&&Result.Transitions.Num()==2&&From.Items[0].Quantity==8&&To.Items[0].Quantity==20&&To.Items[1].Quantity==20);
    To.Items[0].Quantity=19;To.Items[1].Quantity=19;To.Items[0].Quality=1;To.Items[1].Quality=1;
    TestTrue(TEXT("No room in incompatible state groups"),From.TransferTo(To,Item.InstanceId,8,true,D).Code==EAetherInventoryMutationCode::Capacity);
    To.Items[0].InstanceId=Item.InstanceId;
    TestTrue(TEXT("Cross-container duplicated identity rejected"),From.TransferTo(To,Item.InstanceId,1,true,D).Code==EAetherInventoryMutationCode::Invalid);
    TestTrue(TEXT("Cannot alias both sides"),From.TransferTo(From,Item.InstanceId,1,true,D).Code==EAetherInventoryMutationCode::Invalid);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherContainerCodecTest,"Aether.V10.World.ContainerDTOOwnershipAndBounds",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherContainerCodecTest::RunTest(const FString&)
{
    FString Json,Reason;FFileHelper::LoadFileToString(Json,*(FPaths::ProjectContentDir()/TEXT("AetherCore/Definitions/V10/Items.json")));
    const auto D=FAetherV10ItemDefinitions::Parse(Json,Reason);
    FAetherContainerStateV10 C;C.ContainerId=TEXT("Storage.Alice");C.Revision=7;C.Kind=EAetherContainerKind::PersonalStorage;
    C.OwnerCharacterId=TEXT("合成角色");C.RegionId=TEXT("Town");C.Location=FVector(11,22,33);C.Inventory.Capacity=64;
    FAetherV10ItemInstance I;I.InstanceId=FGuid(1,7,3,4);I.DefinitionId=TEXT("Potion");I.Quantity=5;I.SlotIndex=63;I.BoundToCharacter=C.OwnerCharacterId;C.Inventory.Items.Add(I);
    TArray<uint8> Bytes,Again;FAetherContainerStateV10 Out;
    if(!TestTrue(*Reason,AetherContainerCodec::Encode(C,D,Bytes,Reason)&&AetherContainerCodec::Decode(Bytes,D,Out,Reason)))return false;
    TestTrue(TEXT("Capacity, owner, sparse cell and identity survive"),Out.Inventory.Capacity==64&&Out.Inventory.At(63)->InstanceId==I.InstanceId&&Out.Revision==7&&Out.OwnerCharacterId==C.OwnerCharacterId&&Out.Location==C.Location);
    TestTrue(TEXT("Only bound server character allowed"),Out.Allows(C.OwnerCharacterId)&&!Out.Allows(TEXT("Other")));
    TestTrue(TEXT("Canonical container bytes"),AetherContainerCodec::Encode(Out,D,Again,Reason)&&Again==Bytes);
    for(int32 N=0;N<Bytes.Num();++N)
    {
        TArray<uint8> Cut;Cut.Append(Bytes.GetData(),N);TestFalse(TEXT("Every truncated container rejected"),AetherContainerCodec::Decode(Cut,D,Out,Reason));
        TestEqual(TEXT("Failure preserves caller container"),Out.Revision,int64(7));
    }
    Again=Bytes;Again.Add(0);TestFalse(TEXT("Trailing container bytes rejected"),AetherContainerCodec::Decode(Again,D,Out,Reason));
    Again=Bytes;Again[4]=99;TestFalse(TEXT("Unknown container format rejected"),AetherContainerCodec::Decode(Again,D,Out,Reason));
    auto Bad=C;Bad.Kind=EAetherContainerKind::SharedChest;Bad.OwnerCharacterId.Reset();TestFalse(TEXT("Bound item cannot become public loot"),Bad.Validate(D,Reason));
    Bad=C;Bad.OwnerCharacterId=TEXT("Other");TestFalse(TEXT("Bound item cannot move to another personal storage"),Bad.Validate(D,Reason));
    Bad=C;Bad.Inventory.Equipment.Add(TEXT("MainHand"),I.InstanceId);TestFalse(TEXT("Containers never contain active equipment references"),Bad.Validate(D,Reason));
    Bad={};Bad.ContainerId=TEXT("Drop.Empty");Bad.Kind=EAetherContainerKind::WorldDrop;
    TestFalse(TEXT("Empty active drop is invalid"),Bad.Validate(D,Reason));Bad.bActive=false;
    TestTrue(TEXT("Empty drop retains tombstone version"),AetherContainerCodec::Encode(Bad,D,Bytes,Reason)&&AetherContainerCodec::Decode(Bytes,D,Out,Reason)&&!Out.Allows(TEXT("Alice")));
    return true;
}
#endif
