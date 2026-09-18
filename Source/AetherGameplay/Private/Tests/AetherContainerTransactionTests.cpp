#include "Misc/AutomationTest.h"
#include "Persistence/AetherSqliteStore.h"
#include "Contracts/AetherPlayerCommand.h"
#include "Profile/AetherProfileCodec.h"
#include "World/AetherContainerCodec.h"
#include "World/AetherWorldCodec.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherContainerAtomicTest,"Aether.V10.Store.ContainerConcurrentPickupAndRecovery",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherContainerAtomicTest::RunTest(const FString&)
{
    FString Json,Reason;FFileHelper::LoadFileToString(Json,*(FPaths::ProjectContentDir()/TEXT("AetherCore/Definitions/V10/Items.json")));
    const auto Items=FAetherV10ItemDefinitions::Parse(Json,Reason);const auto& Skills=FAetherSkillDefinitionsV10::Get();const auto& Rules=FAetherRules::Get();
    FAetherProfileStateV10 Alice,Bob;Alice.CharacterId=TEXT("Alice");Bob.CharacterId=TEXT("Bob");
    FAetherWorldStateV10 World;const TMap<FString,int64> Versions={{TEXT("Alice"),0},{TEXT("Bob"),0}};
    FAetherContainerStateV10 Drop;Drop.ContainerId=TEXT("Drop.Test");Drop.Kind=EAetherContainerKind::WorldDrop;Drop.Location=FVector(100,200,300);Drop.Inventory.Capacity=1;
    FAetherV10ItemInstance Item;Item.InstanceId=FGuid(1,2,8,9);Item.SlotIndex=0;Item.Quality=2;Item.Affixes.Add(TEXT("Test.Affix"),7);Item.StateGroup=TEXT("Wet");
    for(const auto& D:Items.Items)if(D.Value.MaxDurability>=10){Item.DefinitionId=D.Key;Item.Durability=7;break;}
    if(!TestFalse(TEXT("Production definition supports persisted wear"),Item.DefinitionId.IsEmpty()))return false;
    Drop.Inventory.Items.Add(Item);
    FAetherSqliteOptions O;O.DatabasePath=FPaths::ProjectSavedDir()/TEXT("Automation/V10Container")/FGuid::NewGuid().ToString(EGuidFormats::Digits)/TEXT("state.sqlite");
    O.Fault=MakeShared<TAtomic<EAetherStoreFault>,ESPMode::ThreadSafe>(EAetherStoreFault::None);
    auto DB=AetherSQLite::Open(O);if(!TestTrue(TEXT("Open isolated container store"),DB.Store.IsValid()))return false;
    FAetherTransaction Seed;Seed.ActorId=Alice.CharacterId;Seed.ExpectedProfileRevision=-1;Seed.CommandId=AetherTransactions::NewCommandId(-1);Seed.Request={1};
    for(const auto& P:{Alice,Bob})
    {
        FAetherAggregateWrite W;W.Value.Key={EAetherAggregateKind::Profile,P.CharacterId};
        if(!AetherProfileCodec::Encode(P,Items,Skills,Rules,W.Value.Payload,Reason)){AddError(Reason);DB.Store->Close();return false;}Seed.Writes.Add(W);
    }
    FAetherAggregateWrite W;W.Value.Key={EAetherAggregateKind::World,TEXT("Main")};
    if(!TestTrue(*Reason,AetherWorldCodec::Encode(World,Items,Rules,Versions,W.Value.Payload,Reason))){DB.Store->Close();return false;}Seed.Writes.Add(W);
    W.Value.Key={EAetherAggregateKind::Container,Drop.ContainerId};
    if(!TestTrue(*Reason,AetherContainerCodec::Encode(Drop,Items,W.Value.Payload,Reason))){DB.Store->Close();return false;}Seed.Writes.Add(W);
    TestTrue(TEXT("Seed all synthetic aggregates"),DB.Store->Commit(Seed).Get().Code==EAetherStoreCode::Committed);
    // 两人从同一已提交版本计算候选；最终竞争必须由 SQL 比较全部聚合版本决定。
    const auto Candidate=[&](FAetherProfileStateV10 P)
    {
        auto Container=Drop;auto NextWorld=World;FAetherTransaction T;
        const auto Move=Container.Inventory.TransferTo(P.Inventory,Item.InstanceId,1,true,Items);
        TestTrue(TEXT("Candidate transfers entire original identity"),Move.Code==EAetherInventoryMutationCode::Applied);
        Container.bActive=false;++Container.Revision;++P.Revision;++NextWorld.Revision;
        auto C=FAetherPlayerCommand();C.Type=EAetherCommandType::PickUpItem;C.CommandId=AetherTransactions::NewCommandId(0);
        C.ItemInstanceId=Item.InstanceId;C.TargetStableId=Drop.ContainerId;C.Quantity=1;C.ExpectedWorldRevision=0;
        T.ActorId=P.CharacterId;T.CommandId=C.CommandId;T.ExpectedProfileRevision=0;AetherCommands::Encode(C,T.Request,Reason);
        FAetherCommandResult R;R.CommandId=C.CommandId;R.Code=EAetherCommandCode::Applied;R.FinalProfileRevision=1;R.FinalWorldRevision=1;R.ActualQuantity=1;R.AffectedIds=Move.AffectedIds;
        for(const auto& Transition:Move.Transitions)R.Transfers.Add({Transition.From,Transition.To,Transition.Quantity});
        TestTrue(TEXT("Persistent transfer receipt encodes"),AetherCommands::EncodeResult(R,T.Result,Reason));
        FAetherAggregateWrite Write;Write.ExpectedRevision=0;Write.Value.Revision=1;
        Write.Value.Key={EAetherAggregateKind::Profile,P.CharacterId};AetherProfileCodec::Encode(P,Items,Skills,Rules,Write.Value.Payload,Reason);T.Writes.Add(Write);
        Write.Value.Key={EAetherAggregateKind::World,TEXT("Main")};AetherWorldCodec::Encode(NextWorld,Items,Rules,Versions,Write.Value.Payload,Reason);T.Writes.Add(Write);
        Write.Value.Key={EAetherAggregateKind::Container,Container.ContainerId};AetherContainerCodec::Encode(Container,Items,Write.Value.Payload,Reason);T.Writes.Add(Write);
        return T;
    };
    const auto A=Candidate(Alice),B=Candidate(Bob);
    O.Fault->Store(EAetherStoreFault::AfterFirstWrite);
    TestTrue(TEXT("Injected partial cross-domain write rolls back"),DB.Store->Commit(A).Get().Code==EAetherStoreCode::Unavailable);
    for(const auto& Row:Seed.Writes)
    {
        const auto Existing=DB.Store->Read(Row.Value.Key).Get();
        TestTrue(TEXT("Every source and destination remains at original state"),Existing.Value.IsSet()&&Existing.Value->Revision==0&&Existing.Value->Payload==Row.Value.Payload);
    }
    // 排入同一写队列，先成功者领取；后者不能让另一份相同实例进入自己的 Profile。
    auto First=DB.Store->Commit(A);auto Second=DB.Store->Commit(B);
    TestTrue(TEXT("First claimant commits once"),First.Get().Code==EAetherStoreCode::Committed);
    TestTrue(TEXT("Second stale container/world claimant conflicts"),Second.Get().Code==EAetherStoreCode::Conflict);
    DB.Store->Close();DB.Store.Reset();DB=AetherSQLite::Open(O);
    if(!TestTrue(TEXT("Reopen pickup state"),DB.Store.IsValid()))return false;
    const auto Receipt=DB.Store->Commit(A).Get();FAetherCommandResult Decoded;
    TestTrue(TEXT("Pickup replay preserves exact identity transition"),Receipt.Code==EAetherStoreCode::Replayed&&AetherCommands::DecodeResult(Receipt.Result,Decoded,Reason)&&Decoded.Transfers.Num()==1&&Decoded.Transfers[0].From==Item.InstanceId&&Decoded.Transfers[0].To==Item.InstanceId);
    const auto ARow=DB.Store->Read({EAetherAggregateKind::Profile,TEXT("Alice")}).Get();const auto BRow=DB.Store->Read({EAetherAggregateKind::Profile,TEXT("Bob")}).Get();
    TestTrue(TEXT("Winner restores full item state"),ARow.Value.IsSet()&&AetherProfileCodec::Decode(ARow.Value->Payload,Items,Skills,Rules,Alice,Reason)&&Alice.Inventory.Find(Item.InstanceId)&&Alice.Inventory.Find(Item.InstanceId)->SameStackKey(Item));
    TestTrue(TEXT("Loser profile unchanged"),BRow.Value.IsSet()&&AetherProfileCodec::Decode(BRow.Value->Payload,Items,Skills,Rules,Bob,Reason)&&Bob.Revision==0&&Bob.Inventory.Items.IsEmpty());
    const auto CRow=DB.Store->Read({EAetherAggregateKind::Container,Drop.ContainerId}).Get();
    TestTrue(TEXT("Claimed drop restores inactive versioned tombstone"),CRow.Value.IsSet()&&AetherContainerCodec::Decode(CRow.Value->Payload,Items,Drop,Reason)&&Drop.Revision==1&&!Drop.bActive&&Drop.Inventory.Items.IsEmpty());
    const auto WorldRow=DB.Store->Read({EAetherAggregateKind::World,TEXT("Main")}).Get();TestTrue(TEXT("World revision shares atomic boundary"),WorldRow.Value.IsSet()&&WorldRow.Value->Revision==1);
    DB.Store->Close();return true;
}
#endif
