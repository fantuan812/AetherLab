#include "Misc/AutomationTest.h"
#include "Commands/AetherProfileCoordinator.h"
#include "Commands/AetherContainerCommand.h"
#include "Persistence/AetherSqliteStore.h"
#include "Profile/AetherProfileCodec.h"
#include "World/AetherWorldCodec.h"
#include "World/AetherContainerCodec.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#if WITH_DEV_AUTOMATION_TESTS
namespace
{
FAetherPlayerCommand ContainerCommand(EAetherCommandType Type,int64 Profile,int64 World,FGuid Item,int32 Quantity)
{
    FAetherPlayerCommand C;C.Type=Type;C.CommandId=AetherTransactions::NewCommandId(Profile);
    C.ExpectedProfileRevision=Profile;C.ExpectedWorldRevision=World;C.ItemInstanceId=Item;C.Quantity=Quantity;return C;
}
TArray<FAetherProfileCompletion> Drain(FAetherProfileCoordinator& Service,const FAetherResolveProfileContext& Resolve,int32 Count=1)
{
    TArray<FAetherProfileCompletion> Results;const double Deadline=FPlatformTime::Seconds()+5;
    while(Results.Num()<Count&&FPlatformTime::Seconds()<Deadline)
    {Results.Append(Service.Poll(Resolve));FPlatformProcess::Sleep(.001f);}return Results;
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherContainerCoordinatorTest,"Aether.V10.Commands.ContainerCoordinatorAuthorizationAndCompetition",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherContainerCoordinatorTest::RunTest(const FString&)
{
    FString Json,Reason;FFileHelper::LoadFileToString(Json,*(FPaths::ProjectContentDir()/TEXT("AetherCore/Definitions/V10/Items.json")));
    const auto Items=FAetherV10ItemDefinitions::Parse(Json,Reason);const auto& Skills=FAetherSkillDefinitionsV10::Get();const auto& Rules=FAetherRules::Get();
    FAetherProfileStateV10 Alice,Bob;Alice.CharacterId=TEXT("Alice");Bob.CharacterId=TEXT("Bob");
    FAetherV10ItemInstance I;I.InstanceId=FGuid(1,2,3,4);I.DefinitionId=TEXT("Potion");I.Quantity=10;I.SlotIndex=0;I.Quality=2;I.Affixes.Add(TEXT("Test.Affix"),7);I.StateGroup=TEXT("Wet");Alice.Inventory.Items.Add(I);
    const auto Original=I;
    I.InstanceId=FGuid(4,3,2,1);I.Quantity=2;I.SlotIndex=1;I.BoundToCharacter=Alice.CharacterId;I.bLocked=true;Alice.Inventory.Items.Add(I);const auto Bound=I;
    FAetherWorldStateV10 World;FAetherContainerStateV10 Storage;Storage.ContainerId=TEXT("Storage.Alice");Storage.Kind=EAetherContainerKind::PersonalStorage;Storage.OwnerCharacterId=Alice.CharacterId;Storage.Inventory.Capacity=2;
    FAetherSqliteOptions O;O.DatabasePath=FPaths::ProjectSavedDir()/TEXT("Automation/V10ContainerCoordinator")/FGuid::NewGuid().ToString(EGuidFormats::Digits)/TEXT("state.sqlite");
    auto DB=AetherSQLite::Open(O);if(!TestTrue(TEXT("Open isolated service database"),DB.Store.IsValid()))return false;
    FAetherTransaction Seed;Seed.ActorId=Alice.CharacterId;Seed.ExpectedProfileRevision=-1;Seed.CommandId=AetherTransactions::NewCommandId(-1);Seed.Request={1};
    for(const auto& P:{Alice,Bob})
    {
        FAetherAggregateWrite W;W.Value.Key={EAetherAggregateKind::Profile,P.CharacterId};AetherProfileCodec::Encode(P,Items,Skills,Rules,W.Value.Payload,Reason);Seed.Writes.Add(W);
    }
    const TMap<FString,int64> Versions={{TEXT("Alice"),0},{TEXT("Bob"),0}};
    FAetherAggregateWrite W;W.Value.Key={EAetherAggregateKind::World,TEXT("Main")};AetherWorldCodec::Encode(World,Items,Rules,Versions,W.Value.Payload,Reason);Seed.Writes.Add(W);
    W.Value.Key={EAetherAggregateKind::Container,Storage.ContainerId};AetherContainerCodec::Encode(Storage,Items,W.Value.Payload,Reason);Seed.Writes.Add(W);
    if(!TestTrue(TEXT("Seed profiles world and personal storage"),DB.Store->Commit(Seed).Get().Code==EAetherStoreCode::Committed)){DB.Store->Close();return false;}
    TestTrue(TEXT("Reserved marker is not an exposed version index"),DB.Store->ReadRevisions(EAetherAggregateKind::Migration).Get().Code==EAetherStoreCode::Invalid);
    const auto Index=DB.Store->ReadRevisions(EAetherAggregateKind::Profile).Get();
    TestTrue(TEXT("Bounded index returns exact known profile versions"),Index.Code==EAetherStoreCode::Found&&Index.Revisions.Num()==2&&Index.Revisions.FindRef(TEXT("Alice"))==0);
    FAetherStoreSnapshotQuery Query;Query.Keys={{EAetherAggregateKind::Profile,TEXT("Alice")},{EAetherAggregateKind::Profile,TEXT("Bob")},{EAetherAggregateKind::World,TEXT("Main")},{EAetherAggregateKind::Container,TEXT("Drop.Slot0")}};
    Query.bIncludeProfileRevisions=true;Query.bIncludeContainerCount=true;
    auto Initial=DB.Store->ReadSnapshot(Query).Get();
    TestTrue(TEXT("One read transaction distinguishes missing drop from error"),Initial.Code==EAetherStoreCode::Found&&Initial.Values.Num()==3&&Initial.ContainerCount==1&&Initial.ProfileRevisions.Num()==2);
    auto Duplicate=Query;Duplicate.Keys[1]=Duplicate.Keys[0];TestTrue(TEXT("Duplicate snapshot keys rejected before queue"),DB.Store->ReadSnapshot(Duplicate).Get().Code==EAetherStoreCode::Invalid);
    FAetherProfileCoordinator Service(DB.Store.ToSharedRef(),Items,Skills,Rules);
    auto SessionA=Service.BeginSession(TEXT("Alice"));const auto SessionB=Service.BeginSession(TEXT("Bob"));
    FAetherProfileCommandContext Context;Context.bCanManageInventory=true;
    Context.Container.ContainerId=TEXT("Drop.Slot0");Context.Container.TargetStableId=TEXT("Drop.Actor");Context.Container.RegionId=TEXT("Town");
    Context.Container.bAuthorized=true;Context.Container.bTargetReady=true;Context.Container.bValidDropLocation=true;
    Context.Container.bInventoryPickup=true;Context.Container.bCanWithdraw=true;Context.Container.bSafeToStore=true;Context.Container.DropLocation=FVector(100,200,300);
    int32 Evaluations=0;
    const FAetherResolveProfileContext Resolve=[&](const auto&,const auto&,const auto&,auto& Out){++Evaluations;Out=Context;return true;};
    FAetherCommandResult Rejection;auto Drop=ContainerCommand(EAetherCommandType::DropItem,0,0,Original.InstanceId,4);
    Service.Submit(SessionA,Drop,Rejection);auto Done=Drain(Service,Resolve);
    if(!TestTrue(TEXT("Drop service returns three committed snapshots"),Done.Num()==1&&Done[0].Result.Code==EAetherCommandCode::Applied&&Done[0].Snapshot.IsSet()&&Done[0].WorldSnapshot.IsSet()&&Done[0].ContainerSnapshot.IsSet()))
    {DB.Store->Close();return false;}
    auto FirstDrop=Done[0].ContainerSnapshot.GetValue();const FGuid DropItem=FirstDrop.Inventory.Items[0].InstanceId;
    TestTrue(TEXT("Partial drop preserves metadata and location with a new split identity"),DropItem!=Original.InstanceId&&FirstDrop.Inventory.Items[0].Quantity==4&&FirstDrop.Inventory.Items[0].SameStackKey(Original)&&FirstDrop.Location==Context.Container.DropLocation&&Done[0].Snapshot->Inventory.Find(Original.InstanceId)->Quantity==6);
    TestTrue(TEXT("All aggregate versions exposed after durable readback"),Done[0].Snapshot->Revision==1&&Done[0].WorldSnapshot->Revision==1&&FirstDrop.Revision==0);
    Context.Container.bValidDropLocation=false;auto Invalid=ContainerCommand(EAetherCommandType::DropItem,1,1,Original.InstanceId,1);
    Service.Submit(SessionA,Invalid,Rejection);Done=Drain(Service,Resolve);TestTrue(TEXT("Invalid drop location never consumes items"),Done.Num()==1&&Done[0].Result.Code==EAetherCommandCode::NotAllowed&&Done[0].Result.ActualQuantity==0);
    Context.Container.bValidDropLocation=true;Context.Container.bSafeToStore=false;
    auto PickupB=ContainerCommand(EAetherCommandType::PickUpItem,0,1,DropItem,4);PickupB.TargetStableId=TEXT("Drop.Actor");
    Service.Submit(SessionB,PickupB,Rejection);Done=Drain(Service,Resolve);TestTrue(TEXT("Unsafe world material cannot enter inventory"),Done.Num()==1&&Done[0].Result.Code==EAetherCommandCode::NotAllowed);
    Context.Container.bSafeToStore=true;
    auto PickupA=ContainerCommand(EAetherCommandType::PickUpItem,1,1,DropItem,4);PickupA.TargetStableId=TEXT("Drop.Actor");
    PickupB.CommandId=AetherTransactions::NewCommandId(0);
    Service.Submit(SessionA,PickupA,Rejection);Service.Submit(SessionB,PickupB,Rejection);Done=Drain(Service,Resolve,2);
    int32 Winners=0;FString Winner;
    for(const auto& R:Done)
    {
        if(R.Result.Code==EAetherCommandCode::Applied){++Winners;Winner=R.Session.CharacterId;}
        else TestTrue(TEXT("Losing claimant must synchronize"),R.Result.Code==EAetherCommandCode::Conflict||R.Result.Code==EAetherCommandCode::StaleRevision);
    }
    if(!TestTrue(TEXT("Exactly one concurrent service claimant succeeds"),Done.Num()==2&&Winners==1)){DB.Store->Close();return false;}
    const auto WinningCommand=Winner==TEXT("Alice")?PickupA:PickupB;const auto WinningSession=Winner==TEXT("Alice")?SessionA:SessionB;
    const int32 BeforeReplay=Evaluations;Context.Container.bAuthorized=false;
    Service.Submit(WinningSession,WinningCommand,Rejection);Done=Drain(Service,Resolve);
    TestTrue(TEXT("Committed pickup replays even after the interaction session closes"),Done.Num()==1&&Done[0].Result.Code==EAetherCommandCode::Replayed&&Done[0].ContainerSnapshot.IsSet()&&!Done[0].ContainerSnapshot->bActive);
    TestEqual(TEXT("Replay does not reevaluate live collection permissions"),Evaluations,BeforeReplay);Context.Container.bAuthorized=true;
    auto Latest=DB.Store->ReadSnapshot(Query).Get();
    AetherProfileCodec::Decode(Latest.Values.FindChecked({EAetherAggregateKind::Profile,TEXT("Alice")}).Payload,Items,Skills,Rules,Alice,Reason);
    AetherProfileCodec::Decode(Latest.Values.FindChecked({EAetherAggregateKind::Profile,TEXT("Bob")}).Payload,Items,Skills,Rules,Bob,Reason);
    TestTrue(TEXT("Picked instance exists in exactly one inventory"),(Alice.Inventory.Find(DropItem)!=nullptr)!=(Bob.Inventory.Find(DropItem)!=nullptr));
    TestTrue(TEXT("Version index and loaded rows agree in one snapshot"),Latest.ProfileRevisions[TEXT("Alice")]==Alice.Revision&&Latest.ProfileRevisions[TEXT("Bob")]==Bob.Revision&&Latest.ContainerCount==2);
    // 非活动墓碑可以由服务器选为新落点，避免已领取掉落无限创建新的记录。
    auto Reuse=ContainerCommand(EAetherCommandType::DropItem,Alice.Revision,2,Original.InstanceId,2);
    Service.Submit(SessionA,Reuse,Rejection);Done=Drain(Service,Resolve);
    if(!TestTrue(TEXT("Inactive server-selected drop slot is reusable"),Done.Num()==1&&Done[0].Result.Code==EAetherCommandCode::Applied&&Done[0].ContainerSnapshot.IsSet())){DB.Store->Close();return false;}
    Alice=Done[0].Snapshot.GetValue();
    TestTrue(TEXT("Reuse advances tombstone revision and keeps registry bounded"),Done[0].ContainerSnapshot->Revision==2&&Done[0].ContainerSnapshot->Inventory.Items[0].InstanceId!=DropItem&&DB.Store->ReadRevisions(EAetherAggregateKind::Container).Get().Revisions.Num()==2);
    // 在读事务等待期间目标卸载：第二次现场复验阻止发起写事务。
    auto Withdraw=ContainerCommand(EAetherCommandType::PickUpItem,Bob.Revision,3,Done[0].ContainerSnapshot->Inventory.Items[0].InstanceId,1);Withdraw.TargetStableId=TEXT("Drop.Actor");
    int32 Checks=0;
    const FAetherResolveProfileContext Vanishes=[&](const auto&,const auto&,const auto&,auto& Out){Out=Context;if(++Checks>1)Out.Container.bTargetReady=false;return true;};
    Service.Submit(SessionB,Withdraw,Rejection);Done=Drain(Service,Vanishes);
    TestTrue(TEXT("Unloaded target after read cannot execute"),Done.Num()==1&&Done[0].Result.Code==EAetherCommandCode::NotReady&&Checks==2);
    Context.Container.ContainerId=Storage.ContainerId;Context.Container.TargetStableId=TEXT("Storage.Actor");Context.Container.bContainerSession=true;Context.Container.bCanDeposit=true;
    auto Deposit=ContainerCommand(EAetherCommandType::TransferItem,Alice.Revision,3,Bound.InstanceId,2);Deposit.TargetStableId=Context.Container.TargetStableId;Deposit.ContainerId=Storage.ContainerId;
    Service.Submit(SessionA,Deposit,Rejection);Done=Drain(Service,Resolve);
    TestTrue(TEXT("Owner can store locked bound instance without losing state"),Done.Num()==1&&Done[0].Result.Code==EAetherCommandCode::Applied&&Done[0].ContainerSnapshot.IsSet()&&Done[0].ContainerSnapshot->Inventory.Find(Bound.InstanceId)&&Done[0].ContainerSnapshot->Inventory.Find(Bound.InstanceId)->SameStackKey(Bound));
    auto Steal=ContainerCommand(EAetherCommandType::TransferItem,Bob.Revision,4,Bound.InstanceId,1);Steal.TargetStableId=Context.Container.TargetStableId;Steal.ContainerId=Storage.ContainerId;Steal.TransferDirection=EAetherTransferDirection::FromContainer;
    // 即使测试故意给出错误的已授权标志，持久 owner 仍在候选层二次阻止越权。
    Service.Submit(SessionB,Steal,Rejection);Done=Drain(Service,Resolve);
    TestTrue(TEXT("Persistent ownership blocks forged container context and hides contents"),Done.Num()==1&&Done[0].Result.Code==EAetherCommandCode::Unauthorized&&!Done[0].ContainerSnapshot.IsSet()&&!Done[0].WorldSnapshot.IsSet());
    TestEqual(TEXT("No outstanding container tasks"),Service.PendingCount(),0);
    DB.Store->Close();DB.Store.Reset();DB=AetherSQLite::Open(O);
    if(!TestTrue(TEXT("Reopen service-created container registry"),DB.Store.IsValid()))return false;
    const auto Personal=DB.Store->Read({EAetherAggregateKind::Container,Storage.ContainerId}).Get();
    TestTrue(TEXT("Personal deposit survives restart"),Personal.Value.IsSet()&&AetherContainerCodec::Decode(Personal.Value->Payload,Items,Storage,Reason)&&Storage.Revision==1&&Storage.Inventory.Find(Bound.InstanceId));
    DB.Store->Close();return true;
}
#endif
