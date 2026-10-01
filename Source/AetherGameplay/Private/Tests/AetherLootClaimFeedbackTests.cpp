#include "Misc/AutomationTest.h"
#include "Networking/AetherCommandRuntime.h"
#include "Networking/AetherCommandClient.h"
#include "Framework/AetherPlayerController.h"
#include "Framework/AetherProgression.h"
#include "Characters/AetherFrontierCharacter.h"
#include "Definitions/AetherV10Definitions.h"
#include "Persistence/AetherSqliteStore.h"
#include "Profile/AetherProfileCodec.h"
#include "World/AetherWorldCodec.h"
#include "Quests/AetherQuestProgression.h"
#include "Inventory/AetherResourceGate.h"
#include "AetherEquipmentComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
// Only delivery timing and one damaged read response are controlled here. Every
// candidate, cross-aggregate commit, conflict and receipt comes from real SQLite.
template<typename TResult> struct TLootHeldReply
{
    TFuture<TResult> Actual;
    TUniquePtr<TPromise<TResult>> Reply=MakeUnique<TPromise<TResult>>();
    TOptional<TResult> Captured;
    bool Capture()
    {
        if(Captured.IsSet())return true;
        if(!Actual.IsValid()||!Actual.IsReady())return false;
        Captured=Actual.Get();Actual={};return true;
    }
    void Release()
    {
        if(!Reply)return;
        if(!Captured.IsSet()&&Actual.IsValid()){Captured=Actual.Get();Actual={};}
        Reply->SetValue(Captured.IsSet()?MoveTemp(Captured.GetValue()):TResult());
        Reply.Reset();Captured.Reset();
    }
};
class FLootFeedbackStore final : public IAetherTransactionalStore
{
public:
    explicit FLootFeedbackStore(TSharedRef<IAetherTransactionalStore,ESPMode::ThreadSafe> In):Inner(MoveTemp(In)){}
    TSharedRef<IAetherTransactionalStore,ESPMode::ThreadSafe> Inner;
    TSet<FString> HoldNextSnapshot,HoldNextProfileRead,CorruptNextSnapshot;
    TArray<TUniquePtr<TLootHeldReply<FAetherStoreSnapshotResult>>> Snapshots;
    TArray<TUniquePtr<TLootHeldReply<FAetherStoreReadResult>>> ProfileReads;
    TUniquePtr<TLootHeldReply<FAetherStoreResult>> Proof;
    bool bHoldNextProof=false,bCorruptRefreshAfterCommit=false,bClosed=false;
    int32 Commits=0,ReceiptLookups=0,CorruptedSnapshots=0;
    virtual TFuture<FAetherStoreSnapshotResult> ReadSnapshot(FAetherStoreSnapshotQuery Q) override
    {
        FString Actor;for(const auto& Key:Q.Keys)if(Key.Kind==EAetherAggregateKind::Profile)Actor=Key.Id;
        if(CorruptNextSnapshot.Remove(Actor))
        {
            // Read the actual post-commit snapshot first, then damage the response,
            // never the database or a synthetic transaction outcome.
            auto R=Inner->ReadSnapshot(MoveTemp(Q)).Get();++CorruptedSnapshots;
            R.Code=EAetherStoreCode::Corrupt;R.Values.Reset();R.Detail=TEXT("Injected damaged refresh response");
            TPromise<FAetherStoreSnapshotResult> Reply;auto Future=Reply.GetFuture();Reply.SetValue(MoveTemp(R));return Future;
        }
        if(HoldNextSnapshot.Remove(Actor))
        {
            auto Held=MakeUnique<TLootHeldReply<FAetherStoreSnapshotResult>>();
            Held->Actual=Inner->ReadSnapshot(MoveTemp(Q));auto Future=Held->Reply->GetFuture();Snapshots.Add(MoveTemp(Held));return Future;
        }
        return Inner->ReadSnapshot(MoveTemp(Q));
    }
    virtual TFuture<FAetherStoreResult> Commit(FAetherTransaction T) override
    {
        ++Commits;if(bCorruptRefreshAfterCommit){bCorruptRefreshAfterCommit=false;CorruptNextSnapshot.Add(T.ActorId);}
        return Inner->Commit(MoveTemp(T));
    }
    virtual TFuture<FAetherStoreResult> LookupReceipt(FAetherReceiptQuery Q) override
    {
        ++ReceiptLookups;
        if(bHoldNextProof&&!Proof)
        {
            bHoldNextProof=false;Proof=MakeUnique<TLootHeldReply<FAetherStoreResult>>();
            Proof->Actual=Inner->LookupReceipt(MoveTemp(Q));return Proof->Reply->GetFuture();
        }
        return Inner->LookupReceipt(MoveTemp(Q));
    }
    virtual TFuture<FAetherStoreReadResult> Read(FAetherAggregateKey K) override
    {
        if(K.Kind==EAetherAggregateKind::Profile&&HoldNextProfileRead.Remove(K.Id))
        {
            auto Held=MakeUnique<TLootHeldReply<FAetherStoreReadResult>>();
            Held->Actual=Inner->Read(MoveTemp(K));auto Future=Held->Reply->GetFuture();ProfileReads.Add(MoveTemp(Held));return Future;
        }
        return Inner->Read(MoveTemp(K));
    }
    bool CapturedSnapshots(int32 Count)
    {if(Snapshots.Num()!=Count)return false;for(auto& Held:Snapshots)if(!Held->Capture())return false;return true;}
    void ReleaseSnapshots(){for(auto& Held:Snapshots)Held->Release();Snapshots.Reset();}
    void ReleaseProfileReads(){for(auto& Held:ProfileReads)Held->Release();ProfileReads.Reset();}
    void ReleaseProof(){if(Proof){Proof->Release();Proof.Reset();}}
    virtual TFuture<FAetherStoreRevisionIndex> ReadRevisions(EAetherAggregateKind K) override {return Inner->ReadRevisions(K);}
    virtual TFuture<FAetherStoreEffectsResult> PendingEffects(FString Id) override {return Inner->PendingEffects(MoveTemp(Id));}
    virtual TFuture<bool> AcknowledgeEffect(FString Id,FGuid Delivery) override {return Inner->AcknowledgeEffect(MoveTemp(Id),Delivery);}
    virtual TFuture<bool> Backup(FString Path) override {return Inner->Backup(MoveTemp(Path));}
    virtual void Close() override
    {if(bClosed)return;bClosed=true;ReleaseSnapshots();ReleaseProfileReads();ReleaseProof();Inner->Close();}
    virtual ~FLootFeedbackStore() override {Close();}
};
struct FLootFeedbackPlayer
{
    AAetherPlayerController* Controller=nullptr;
    AAetherPlayerState* State=nullptr;
    AAetherFrontierCharacter* Pawn=nullptr;
    UAetherCommandClient* Client=nullptr;
};
FLootFeedbackPlayer MakeLootPlayer(UGameInstance& GI,int32 Index,const FString& Id,FString& Why)
{
    FLootFeedbackPlayer P;auto* Local=GI.CreateLocalPlayer(Index,Why,false);if(!Local)return P;
    auto* W=GI.GetWorld();FActorSpawnParameters Spawn;Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    P.Controller=W->SpawnActor<AAetherPlayerController>();P.State=W->SpawnActor<AAetherPlayerState>();
    P.Pawn=W->SpawnActor<AAetherFrontierCharacter>(FVector(Index*400,0,0),FRotator::ZeroRotator,Spawn);
    if(!P.Controller||!P.State||!P.Pawn)return P;
    P.State->Profile.CharacterId=Id;P.State->SetOwner(P.Controller);P.Controller->PlayerState=P.State;
    P.State->AbilitySystem->AddAttributeSetSubobject(P.State->Attributes.Get());
    auto* Catalog=NewObject<UAetherEquipmentCatalog>(P.Pawn);
    auto* Item=NewObject<UAetherEquipmentDefinition>(Catalog);Item->ItemId="FixtureAccessory";Item->Slot="Neck";Item->bInvisibleAccessory=true;
    Catalog->Items.Add(Item);P.Pawn->Equipment->Catalog=Catalog;
    P.Controller->SetPlayer(Local);P.Controller->Possess(P.Pawn);P.Pawn->SetPlayerState(P.State);P.Pawn->BindPersistentAbilities();
    P.Client=Local->GetSubsystem<UAetherCommandClient>();return P;
}
bool ReplaceLootPawn(FLootFeedbackPlayer& P)
{
    auto* W=P.Controller->GetWorld();auto* Catalog=P.Pawn->Equipment->Catalog.Get();P.Controller->UnPossess();
    FActorSpawnParameters Spawn;Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Replacement=W->SpawnActor<AAetherFrontierCharacter>(FVector(800,0,0),FRotator::ZeroRotator,Spawn);if(!Replacement)return false;
    Replacement->Equipment->Catalog=Catalog;P.Pawn=Replacement;P.Controller->Possess(P.Pawn);
    P.Pawn->SetPlayerState(P.State);P.Pawn->BindPersistentAbilities();return true;
}
bool LootRuntimeIdle(const UAetherCommandRuntime& Runtime)
{const auto M=Runtime.Inspect();return M.PendingCommands==0&&M.PendingFacts==0&&M.DeferredFacts==0;}
bool PumpLootRuntime(UAetherCommandRuntime& Runtime,TFunctionRef<bool()> Done)
{
    const double End=FPlatformTime::Seconds()+5;
    do{Runtime.Tick(.04f);if(Done())return true;FPlatformProcess::Sleep(.001f);}while(FPlatformTime::Seconds()<End);return false;
}
int32 LootItemCount(const FAetherProfileStateV10& P,const FString& Id)
{int32 Count=0;for(const auto& Item:P.Inventory.Items)if(Item.DefinitionId==Id)Count+=Item.Quantity;return Count;}
FAetherWorldLootV10 MakeLoot(const FString& ClaimedBy={})
{FAetherWorldLootV10 L;L.ClaimId=FGuid::NewGuid();L.Definition=TEXT("IronCuirass");L.ClaimedBy=ClaimedBy;return L;}
FAetherProfileStateV10 MakeLootProfile(const FString& Id)
{FAetherProfileStateV10 P;P.CharacterId=Id;P.Inventory.Capacity=FAetherV10Definitions::Get().Items.DefaultCapacity;return P;}
const TCHAR* LootMessage(EAetherLootClaimOutcome Outcome)
{
    switch(Outcome)
    {
    case EAetherLootClaimOutcome::Applied:return TEXT("领取已保存，物品以同步的库存快照为准。");
    case EAetherLootClaimOutcome::AlreadyOwned:return TEXT("这份战利品已由你领取，不会重复发放。");
    case EAetherLootClaimOutcome::InventoryFull:return TEXT("背包空间不足，战利品仍留在原处；整理背包后可重试。");
    case EAetherLootClaimOutcome::ClaimedByOther:return TEXT("这份战利品已被其他玩家领取。");
    case EAetherLootClaimOutcome::Missing:return TEXT("这份战利品已不存在，请刷新目标。");
    default:return TEXT("领取未能确认，请稍后重试或重新同步库存。");
    }
}
struct FLootFeedbackFixture
{
    FAetherSqliteOptions Options;
    TSharedPtr<FLootFeedbackStore,ESPMode::ThreadSafe> Store;
    UGameInstance* GI=nullptr;UWorld* World=nullptr;UAetherCommandRuntime* Runtime=nullptr;
    TArray<FLootFeedbackPlayer> Players;
    TMap<FString,TArray<int64>> PublishedProfiles;
    FAetherWorldStateV10 InitialWorld;
    FString Why;
    bool Init(TArray<FAetherProfileStateV10> Profiles,FAetherWorldStateV10 InWorld)
    {
        const auto& D=FAetherV10Definitions::Get();if(!D.bValid||Profiles.IsEmpty())return false;
        InitialWorld=MoveTemp(InWorld);if(!InitialWorld.RealmId.IsValid())InitialWorld.RealmId=FGuid::NewGuid();
        TMap<FString,int64> Revisions;
        for(auto& P:Profiles)
        {
            const auto R=AetherQuestProgression::Settle(P,InitialWorld.WorldFactSources,{},D.Items,D.Skills,D.Rules,D.Progression);
            if(R.Code!=EAetherQuestMutationCode::Applied&&R.Code!=EAetherQuestMutationCode::Unchanged)return false;
            Revisions.Add(P.CharacterId,P.Revision);
        }
        Options.DatabasePath=FPaths::ProjectSavedDir()/TEXT("Automation/LootClaimFeedback")/FGuid::NewGuid().ToString(EGuidFormats::Digits)/TEXT("state.sqlite");
        Options.Fault=MakeShared<TAtomic<EAetherStoreFault>,ESPMode::ThreadSafe>(EAetherStoreFault::None);
        auto Open=AetherSQLite::Open(Options);if(!Open.Store.IsValid()){Why=TEXT("SQLite open failed");return false;}
        Store=MakeShared<FLootFeedbackStore,ESPMode::ThreadSafe>(Open.Store.ToSharedRef());
        FAetherTransaction Seed;Seed.ActorId=Profiles[0].CharacterId;Seed.ExpectedProfileRevision=-1;
        Seed.CommandId=AetherTransactions::NewCommandId(-1);Seed.Request={1};
        for(const auto& P:Profiles)
        {
            FAetherAggregateWrite Row;Row.Value.Key={EAetherAggregateKind::Profile,P.CharacterId};
            if(!AetherProfileCodec::Encode(P,D.Items,D.Skills,D.Rules,Row.Value.Payload,Why))return false;Seed.Writes.Add(MoveTemp(Row));
        }
        FAetherAggregateWrite Row;Row.Value.Key={EAetherAggregateKind::World,TEXT("Main")};
        if(!AetherWorldCodec::Encode(InitialWorld,D.Items,D.Rules,Revisions,Row.Value.Payload,Why))return false;Seed.Writes.Add(MoveTemp(Row));
        if(Store->Commit(MoveTemp(Seed)).Get().Code!=EAetherStoreCode::Committed)return false;
        GI=NewObject<UGameInstance>(GEngine);GI->AddToRoot();GI->InitializeStandalone(NAME_None,nullptr);
        World=GI->GetWorld();Runtime=GI->GetSubsystem<UAetherCommandRuntime>();if(!World||!Runtime)return false;
        const FAetherResolveConnectedContext Resolve=[](auto&,const auto&,const auto&,auto&){return false;};
        const FAetherPublishConnectedState Publish=[this](auto& PC,const auto& P,const auto*,const auto*)
        {PublishedProfiles.FindOrAdd(P.CharacterId).Add(P.Revision);FString Reason;return PC.template GetPlayerState<AAetherPlayerState>()->PublishNativeProfile(P,Reason);};
        if(!Runtime->InstallBackend(Store.ToSharedRef(),InitialWorld,Resolve,Publish,Why))return false;
        for(int32 I=0;I<Profiles.Num();++I)
        {
            Players.Add(MakeLootPlayer(*GI,I,Profiles[I].CharacterId,Why));auto& P=Players.Last();
            if(!P.Client||!Runtime->BindVerifiedPlayer(P.Controller,Profiles[I].CharacterId))return false;
        }
        if(!PumpLootRuntime(*Runtime,[this]{return Ready();}))return false;
        Store->Commits=0;Store->ReceiptLookups=0;return true;
    }
    bool Ready() const
    {
        if(!Runtime||!LootRuntimeIdle(*Runtime))return false;
        for(const auto& P:Players)if(!P.Client||!P.Client->GetProfile().IsSet()||!P.Pawn->ResourceGate->IsPresentationSettled())return false;
        return true;
    }
    bool ReadProfile(const FString& Id,FAetherProfileStateV10& P)
    {
        const auto& D=FAetherV10Definitions::Get();const auto R=Store->Inner->Read({EAetherAggregateKind::Profile,Id}).Get();
        return R.Code==EAetherStoreCode::Found&&R.Value.IsSet()&&AetherProfileCodec::Decode(R.Value->Payload,D.Items,D.Skills,D.Rules,P,Why)&&P.Revision==R.Value->Revision;
    }
    bool ReadWorld(FAetherWorldStateV10& W)
    {
        const auto& D=FAetherV10Definitions::Get();FAetherStoreSnapshotQuery Q;Q.Keys={{EAetherAggregateKind::World,TEXT("Main")}};Q.bIncludeProfileRevisions=true;
        const auto R=Store->Inner->ReadSnapshot(MoveTemp(Q)).Get();const auto* Row=R.Values.Find({EAetherAggregateKind::World,TEXT("Main")});
        return R.Code==EAetherStoreCode::Found&&Row&&AetherWorldCodec::Decode(Row->Payload,D.Items,D.Rules,R.ProfileRevisions,W,Why)&&W.Revision==Row->Revision;
    }
    ~FLootFeedbackFixture()
    {
        if(Store){Store->ReleaseSnapshots();Store->ReleaseProfileReads();Store->ReleaseProof();}
        if(Runtime)Runtime->DrainBackend();
        if(GI){GI->Shutdown();if(World){GEngine->DestroyWorldContext(World);World->DestroyWorld(false);}GI->RemoveFromRoot();}
        if(Store)Store->Close();
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherLootClaimAtomicFeedbackTest,"Aether.V10.Network.LootClaimAtomicDomainFeedback",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherLootClaimAtomicFeedbackTest::RunTest(const FString&)
{
    const auto& D=FAetherV10Definitions::Get();if(!TestTrue(TEXT("Definitions ready"),D.bValid))return false;
    const auto* Armor=D.Items.Items.Find(TEXT("IronCuirass"));const auto* Material=D.Items.Items.Find(TEXT("Material"));
    if(!TestTrue(TEXT("Bundle definitions have valid stack rules"),Armor&&Armor->MaxStack==1&&Material&&Material->MaxStack>=1))return false;
    auto Full=MakeLootProfile(TEXT("Alice"));auto Bob=MakeLootProfile(TEXT("Bob"));
    for(int32 I=0;I<Full.Inventory.Capacity-1;++I)
        if(!TestTrue(TEXT("Leave exactly one slot in real nonstackable inventory"),Full.Inventory.AddNew(TEXT("IronCuirass"),1,D.Items).Code==EAetherInventoryMutationCode::Applied))return false;
    FAetherWorldStateV10 W;auto Loot=MakeLoot();Loot.Items={{TEXT("IronCuirass"),1},{TEXT("Material"),1}};W.Loot.Add(Loot);
    FLootFeedbackFixture F;if(!TestTrue(TEXT("Real SQLite runtime with two ready owners"),F.Init({Full,Bob},W))){AddError(F.Why);return false;}
    auto& A=F.Players[0];auto& B=F.Players[1];
    const auto& BeforeNative=A.Client->GetProfile().GetValue();auto CapacityProbe=BeforeNative.Inventory;
    if(!TestTrue(TEXT("Persisted fixture has exactly one free slot and no material stack"),
        CapacityProbe.Items.Num()==CapacityProbe.Capacity-1&&LootItemCount(BeforeNative,TEXT("Material"))==0)||
        !TestTrue(TEXT("First sorted bundle entry fits before the later failure"),CapacityProbe.AddNew(TEXT("IronCuirass"),1,D.Items).Code==EAetherInventoryMutationCode::Applied)||
        !TestTrue(TEXT("Second sorted entry needs another slot and fails capacity"),CapacityProbe.AddNew(TEXT("Material"),1,D.Items).Code==EAetherInventoryMutationCode::Capacity))return false;
    const auto BeforeProfile=F.Store->Inner->Read({EAetherAggregateKind::Profile,TEXT("Alice")}).Get();
    const auto BeforeWorld=F.Store->Inner->Read({EAetherAggregateKind::World,TEXT("Main")}).Get();
    const FGuid FullOrigin=FGuid::NewGuid();A.Pawn->Feedback=TEXT("Waiting for final claim result");
    if(!TestTrue(TEXT("Insufficient bundle capacity still receives asynchronous final decision"),F.Runtime->SubmitLootClaim(A.Controller,Loot.ClaimId,FullOrigin,F.Why))||
        !TestTrue(TEXT("Partially fitting bundle claim completes"),PumpLootRuntime(*F.Runtime,[&]{return LootRuntimeIdle(*F.Runtime);})))return false;
    TestEqual(TEXT("Owner is told capacity failure, not provisional success"),A.Pawn->Feedback,FString(LootMessage(EAetherLootClaimOutcome::InventoryFull)));
    const auto AfterProfile=F.Store->Inner->Read({EAetherAggregateKind::Profile,TEXT("Alice")}).Get();
    const auto AfterWorld=F.Store->Inner->Read({EAetherAggregateKind::World,TEXT("Main")}).Get();
    if(!TestTrue(TEXT("Native aggregates remain readable"),BeforeProfile.Value.IsSet()&&BeforeWorld.Value.IsSet()&&AfterProfile.Value.IsSet()&&AfterWorld.Value.IsSet()))return false;
    TestTrue(TEXT("Later capacity failure leaks none of the first item into profile bytes"),BeforeProfile.Value->Payload==AfterProfile.Value->Payload&&BeforeProfile.Value->Revision==AfterProfile.Value->Revision);
    TestTrue(TEXT("Rejected whole bundle leaves loot owner and world bytes unchanged"),BeforeWorld.Value->Payload==AfterWorld.Value->Payload&&BeforeWorld.Value->Revision==AfterWorld.Value->Revision);
    TestEqual(TEXT("Rejected candidate never reaches Commit"),F.Store->Commits,0);
    const FGuid Origin=FGuid::NewGuid();
    if(!TestTrue(TEXT("Other player may claim the still-present loot"),F.Runtime->SubmitLootClaim(B.Controller,Loot.ClaimId,Origin,F.Why))||
        !TestTrue(TEXT("Successful claim publishes the complete bundle snapshot"),PumpLootRuntime(*F.Runtime,[&]{return F.Ready()&&
            LootItemCount(B.Client->GetProfile().GetValue(),TEXT("IronCuirass"))==1&&LootItemCount(B.Client->GetProfile().GetValue(),TEXT("Material"))==1;})))return false;
    TestEqual(TEXT("Final success states durable completion"),B.Pawn->Feedback,FString(LootMessage(EAetherLootClaimOutcome::Applied)));
    FAetherProfileStateV10 Disk;FAetherWorldStateV10 DiskWorld;
    if(!TestTrue(TEXT("Read actual committed reward and claim"),F.ReadProfile(TEXT("Bob"),Disk)&&F.ReadWorld(DiskWorld)))return false;
    TestEqual(TEXT("Exactly one actual inventory reward"),LootItemCount(Disk,TEXT("IronCuirass")),1);
    TestEqual(TEXT("Second bundle entry also commits exactly once"),LootItemCount(Disk,TEXT("Material")),1);
    TestEqual(TEXT("World claim committed to the same owner"),DiskWorld.Loot[0].ClaimedBy,FString(TEXT("Bob")));
    const int64 Revision=Disk.Revision,WorldRevision=DiskWorld.Revision;
    int32 Presentations=0;const auto Handle=B.Pawn->OnPresentationChanged.AddLambda([&]{++Presentations;});
    B.Controller->ClientV10LootClaimResult_Implementation(B.Client->GetChannel(),Origin,Loot.ClaimId,EAetherLootClaimOutcome::Applied);
    B.Controller->ClientV10LootClaimResult_Implementation(B.Client->GetChannel(),Origin,Loot.ClaimId,EAetherLootClaimOutcome::Applied);
    TestEqual(TEXT("Duplicate delivered notification does not rebroadcast presentation"),Presentations,0);
    B.Controller->ClientV10LootClaimResult_Implementation(FGuid::NewGuid(),FGuid::NewGuid(),Loot.ClaimId,EAetherLootClaimOutcome::Invalid);
    TestEqual(TEXT("Foreign channel cannot overwrite final feedback"),Presentations,0);B.Pawn->OnPresentationChanged.Remove(Handle);
    if(!TestTrue(TEXT("Explicit later claim reaches durable AlreadyOwned decision"),F.Runtime->SubmitLootClaim(B.Controller,Loot.ClaimId,FGuid::NewGuid(),F.Why))||
        !TestTrue(TEXT("AlreadyOwned completes"),PumpLootRuntime(*F.Runtime,[&]{return LootRuntimeIdle(*F.Runtime);})))return false;
    TestEqual(TEXT("Repeat claimant sees AlreadyOwned"),B.Pawn->Feedback,FString(LootMessage(EAetherLootClaimOutcome::AlreadyOwned)));
    if(!TestTrue(TEXT("Read after notification and request replay"),F.ReadProfile(TEXT("Bob"),Disk)&&F.ReadWorld(DiskWorld)))return false;
    TestTrue(TEXT("Replay cannot increment revisions or duplicate either bundle entry"),Disk.Revision==Revision&&DiskWorld.Revision==WorldRevision&&
        LootItemCount(Disk,TEXT("IronCuirass"))==1&&LootItemCount(Disk,TEXT("Material"))==1);
    if(!TestTrue(TEXT("Other claimant obtains final ownership rejection"),F.Runtime->SubmitLootClaim(A.Controller,Loot.ClaimId,FGuid::NewGuid(),F.Why))||
        !TestTrue(TEXT("Ownership rejection completes"),PumpLootRuntime(*F.Runtime,[&]{return LootRuntimeIdle(*F.Runtime);})))return false;
    TestEqual(TEXT("Ownership wins over this player's full-bag condition"),A.Pawn->Feedback,FString(LootMessage(EAetherLootClaimOutcome::ClaimedByOther)));
    if(!TestTrue(TEXT("Missing instance may be resolved asynchronously"),F.Runtime->SubmitLootClaim(A.Controller,FGuid::NewGuid(),FGuid::NewGuid(),F.Why))||
        !TestTrue(TEXT("Missing instance completes"),PumpLootRuntime(*F.Runtime,[&]{return LootRuntimeIdle(*F.Runtime);})))return false;
    TestEqual(TEXT("Missing is distinct from inventory capacity"),A.Pawn->Feedback,FString(LootMessage(EAetherLootClaimOutcome::Missing)));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherLootClaimConcurrentFeedbackTest,"Aether.V10.Network.LootClaimConcurrentOwnersAndExactPending",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherLootClaimConcurrentFeedbackTest::RunTest(const FString&)
{
    FAetherWorldStateV10 W;const auto Loot=MakeLoot();W.Loot.Add(Loot);FLootFeedbackFixture F;
    if(!TestTrue(TEXT("Two real connected claimants"),F.Init({MakeLootProfile(TEXT("Alice")),MakeLootProfile(TEXT("Bob"))},W))){AddError(F.Why);return false;}
    auto& A=F.Players[0];auto& B=F.Players[1];F.Store->HoldNextSnapshot={TEXT("Alice"),TEXT("Bob")};
    if(!TestTrue(TEXT("Alice claim accepted"),F.Runtime->SubmitLootClaim(A.Controller,Loot.ClaimId,FGuid::NewGuid(),F.Why))||
        !TestTrue(TEXT("Same instance for different character is independent pending identity"),F.Runtime->SubmitLootClaim(B.Controller,Loot.ClaimId,FGuid::NewGuid(),F.Why)))return false;
    TestFalse(TEXT("Second origin cannot overwrite Alice's pending correlation"),F.Runtime->SubmitLootClaim(A.Controller,Loot.ClaimId,FGuid::NewGuid(),F.Why));
    if(!TestTrue(TEXT("Both candidates read the exact same unclaimed durable world"),PumpLootRuntime(*F.Runtime,[&]{return F.Store->CapturedSnapshots(2);})))return false;
    F.Store->ReleaseSnapshots();
    if(!TestTrue(TEXT("Conflict loser retries through real SQLite"),PumpLootRuntime(*F.Runtime,[&]{return F.Ready();})))return false;
    FAetherProfileStateV10 PA,PB;FAetherWorldStateV10 Disk;
    if(!TestTrue(TEXT("Read both profiles and final world"),F.ReadProfile(TEXT("Alice"),PA)&&F.ReadProfile(TEXT("Bob"),PB)&&F.ReadWorld(Disk)))return false;
    TestEqual(TEXT("Exactly one reward across both claimants"),LootItemCount(PA,TEXT("IronCuirass"))+LootItemCount(PB,TEXT("IronCuirass")),1);
    TestTrue(TEXT("Both immutable candidates reached the real CAS transaction path"),F.Store->Commits>=2);
    const bool AliceWon=Disk.Loot[0].ClaimedBy==TEXT("Alice");
    TestTrue(TEXT("Winner is one of the authenticated owners"),AliceWon||Disk.Loot[0].ClaimedBy==TEXT("Bob"));
    TestEqual(TEXT("Winner receives final Applied"),(AliceWon?A:B).Pawn->Feedback,FString(LootMessage(EAetherLootClaimOutcome::Applied)));
    TestEqual(TEXT("Loser receives final ClaimedByOther"),(AliceWon?B:A).Pawn->Feedback,FString(LootMessage(EAetherLootClaimOutcome::ClaimedByOther)));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherLootClaimCertaintyFeedbackTest,"Aether.V10.Network.LootClaimRequiresCommitProofAndFreshPublication",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherLootClaimCertaintyFeedbackTest::RunTest(const FString&)
{
    for(const bool DamageRefresh:{false,true})
    {
        FAetherWorldStateV10 W;const auto Loot=MakeLoot();W.Loot.Add(Loot);FLootFeedbackFixture F;
        if(!TestTrue(TEXT("Ready real claim fixture"),F.Init({MakeLootProfile(TEXT("Alice"))},W))){AddError(F.Why);return false;}
        auto& A=F.Players[0];const FString Waiting=TEXT("Waiting for durable proof");A.Pawn->Feedback=Waiting;
        const int64 BeforeRevision=A.Client->GetProfile()->Revision;const int32 Publications=F.PublishedProfiles.FindChecked(TEXT("Alice")).Num();
        int32 WorldPublications=0,SuccessNotices=0;bool RecoveredClaim=false;
        F.Runtime->SetWorldPublisher([&](const auto& World)
        {
            ++WorldPublications;const auto* Published=World.Loot.FindByPredicate([&](const auto& L){return L.ClaimId==Loot.ClaimId;});
            RecoveredClaim|=Published&&Published->ClaimedBy==TEXT("Alice");
        });
        const auto NoticeHandle=A.Pawn->OnPresentationChanged.AddLambda([&]
        {if(A.Pawn->Feedback==LootMessage(EAetherLootClaimOutcome::Applied))++SuccessNotices;});
        ON_SCOPE_EXIT {A.Pawn->OnPresentationChanged.Remove(NoticeHandle);F.Runtime->SetWorldPublisher({});};
        F.Options.Fault->Store(EAetherStoreFault::AfterCommitBeforeReply);F.Store->bHoldNextProof=true;
        F.Store->bCorruptRefreshAfterCommit=DamageRefresh;
        if(DamageRefresh)F.Store->HoldNextProfileRead.Add(TEXT("Alice"));
        if(!TestTrue(TEXT("Claim accepted before lost response"),F.Runtime->SubmitLootClaim(A.Controller,Loot.ClaimId,FGuid::NewGuid(),F.Why))||
            !TestTrue(TEXT("Original transaction's real receipt has been queried"),PumpLootRuntime(*F.Runtime,[&]{return F.Store->Proof&&F.Store->Proof->Capture();})))return false;
        TestTrue(TEXT("SQLite consumed after-commit response-loss fault"),F.Options.Fault->Load()==EAetherStoreFault::None);
        FAetherProfileStateV10 Disk;FAetherWorldStateV10 DiskWorld;
        if(!TestTrue(TEXT("Reward really committed despite unavailable response"),F.ReadProfile(TEXT("Alice"),Disk)&&F.ReadWorld(DiskWorld)))return false;
        TestTrue(TEXT("Inventory and claim are one durable result"),LootItemCount(Disk,TEXT("IronCuirass"))==1&&DiskWorld.Loot[0].ClaimedBy==TEXT("Alice"));
        TestEqual(TEXT("No provisional success or timeout failure while proof is held"),A.Pawn->Feedback,Waiting);
        TestEqual(TEXT("Unconfirmed candidate cannot be published"),F.PublishedProfiles.FindChecked(TEXT("Alice")).Num(),Publications);
        TestEqual(TEXT("Unconfirmed candidate world cannot be published"),WorldPublications,0);
        F.Store->ReleaseProof();
        if(!TestTrue(TEXT("Confirmed completion produces final feedback"),PumpLootRuntime(*F.Runtime,[&]{return A.Pawn->Feedback!=Waiting;})))return false;
        TestEqual(TEXT("Lost response must resolve to success, never fake failure"),A.Pawn->Feedback,FString(LootMessage(EAetherLootClaimOutcome::Applied)));
        if(DamageRefresh)
        {
            TestEqual(TEXT("Post-proof refresh response was deliberately damaged"),F.Store->CorruptedSnapshots,1);
            TestEqual(TEXT("Damaged refresh cannot publish a candidate fallback"),F.PublishedProfiles.FindChecked(TEXT("Alice")).Num(),Publications);
            TestEqual(TEXT("Damaged refresh cannot publish an old or candidate world"),WorldPublications,0);
            TestEqual(TEXT("Client keeps its last verified snapshot until fresh read"),A.Client->GetProfile()->Revision,BeforeRevision);
            TestEqual(TEXT("Runtime requested one independent latest profile read"),F.Store->ProfileReads.Num(),1);
            F.Store->ReleaseProfileReads();
        }
        if(!TestTrue(TEXT("Verified latest snapshot eventually reaches owner"),PumpLootRuntime(*F.Runtime,[&]
            {return F.Ready()&&A.Client->GetProfile()->Revision==Disk.Revision&&LootItemCount(A.Client->GetProfile().GetValue(),TEXT("IronCuirass"))==1;})))return false;
        TestTrue(TEXT("Formal verified publication restores the committed world claim"),RecoveredClaim);
        TestEqual(TEXT("World restored once, including Settle after a damaged refresh"),WorldPublications,1);
        TestEqual(TEXT("Recovery does not send a second success notification"),SuccessNotices,1);
        TestEqual(TEXT("Proof retries do not resubmit or duplicate committed reward"),F.Store->Commits,1);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherLootClaimLifecycleFeedbackTest,"Aether.V10.Network.LootClaimRevokesStaleSessionPawnAndRealmFeedback",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherLootClaimLifecycleFeedbackTest::RunTest(const FString&)
{
    // 0: reconnect, 1: pawn replacement, 2: backend exit while transaction is live.
    for(const int32 Scenario:{0,1,2})
    {
        FAetherWorldStateV10 W;const auto Loot=MakeLoot();W.Loot.Add(Loot);FLootFeedbackFixture F;
        if(!TestTrue(TEXT("Ready lifecycle fixture"),F.Init({MakeLootProfile(TEXT("Alice"))},W))){AddError(F.Why);return false;}
        auto& A=F.Players[0];auto* OldPawn=A.Pawn;const auto OldChannel=A.Client->GetChannel();const FGuid Origin=FGuid::NewGuid();
        const FString Sentinel=TEXT("No feedback from the departed request");OldPawn->Feedback=Sentinel;F.Store->HoldNextSnapshot.Add(TEXT("Alice"));
        if(!TestTrue(TEXT("Old lifetime claim accepted"),F.Runtime->SubmitLootClaim(A.Controller,Loot.ClaimId,Origin,F.Why))||
            !TestTrue(TEXT("Hold actual old-lifetime read before commit"),PumpLootRuntime(*F.Runtime,[&]{return F.Store->CapturedSnapshots(1);})))return false;
        if(Scenario==0)F.Runtime->UnbindPlayer(A.Controller);
        if(Scenario==2)F.Runtime->UninstallBackend();
        else
        {
            if(!TestTrue(TEXT("Create fresh actual avatar lifetime"),ReplaceLootPawn(A)))return false;A.Pawn->Feedback=Sentinel;
            if(Scenario==0&&!TestTrue(TEXT("Same identity reconnects while old transaction continues"),F.Runtime->BindVerifiedPlayer(A.Controller,TEXT("Alice"))))return false;
            TestTrue(TEXT("Fresh avatar uses different owner channel"),A.Client->GetChannel().IsValid()&&A.Client->GetChannel()!=OldChannel);
        }
        F.Store->ReleaseSnapshots();
        if(Scenario==2)
        {
            if(!TestTrue(TEXT("Backend drains accepted transaction after revoking feedback"),F.Runtime->DrainBackend()))return false;
            auto Open=AetherSQLite::Open(F.Options);if(!TestTrue(TEXT("Reopen durable state after shutdown"),Open.Store.IsValid()))return false;
            F.Store=MakeShared<FLootFeedbackStore,ESPMode::ThreadSafe>(Open.Store.ToSharedRef());
        }
        else if(!TestTrue(TEXT("New owner life recovers committed snapshot"),PumpLootRuntime(*F.Runtime,[&]
            {return F.Ready()&&LootItemCount(A.Client->GetProfile().GetValue(),TEXT("IronCuirass"))==1;})))return false;
        FAetherProfileStateV10 Disk;FAetherWorldStateV10 DiskWorld;
        if(!TestTrue(TEXT("Revocation never cancels the accepted transaction"),F.ReadProfile(TEXT("Alice"),Disk)&&F.ReadWorld(DiskWorld)))return false;
        TestTrue(TEXT("Reward remains durably owned exactly once"),LootItemCount(Disk,TEXT("IronCuirass"))==1&&DiskWorld.Loot[0].ClaimedBy==TEXT("Alice"));
        TestEqual(TEXT("Departed Pawn does not receive final notification"),OldPawn->Feedback,Sentinel);
        TestEqual(TEXT("Replacement life does not inherit old request feedback"),A.Pawn->Feedback,Sentinel);
        A.Controller->ClientV10LootClaimResult_Implementation(OldChannel,Origin,Loot.ClaimId,EAetherLootClaimOutcome::Applied);
        TestEqual(TEXT("Late transport replay of old channel is rejected"),A.Pawn->Feedback,Sentinel);
        // Change the client's confirmed realm/channel without inventing a server
        // claim. A late old-realm packet is transport input only, never authority.
        const FGuid NewRealm=FGuid::NewGuid(),NewChannel=FGuid::NewGuid();
        A.Client->ReceiveChannel(A.Controller,NewChannel,TEXT("Alice"),NewRealm);
        A.Controller->ClientV10LootClaimResult_Implementation(OldChannel,FGuid::NewGuid(),Loot.ClaimId,EAetherLootClaimOutcome::Applied);
        TestEqual(TEXT("Old-realm result cannot reach the next realm's UI"),A.Pawn->Feedback,Sentinel);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherLootClaimReentrantBatchFeedbackTest,"Aether.V10.Network.LootClaimDetachesWholeCompletionBatchBeforePublication",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherLootClaimReentrantBatchFeedbackTest::RunTest(const FString&)
{
    const auto& D=FAetherV10Definitions::Get();auto Alice=MakeLootProfile(TEXT("Alice")),Bob=MakeLootProfile(TEXT("Bob"));
    if(!TestTrue(TEXT("Seed owners actually possess their previously claimed loot"),
        Alice.Inventory.AddNew(TEXT("IronCuirass"),1,D.Items).Code==EAetherInventoryMutationCode::Applied&&
        Bob.Inventory.AddNew(TEXT("IronCuirass"),1,D.Items).Code==EAetherInventoryMutationCode::Applied))return false;
    FAetherWorldStateV10 W;const auto LA=MakeLoot(TEXT("Alice")),LB=MakeLoot(TEXT("Bob"));W.Loot={LA,LB};FLootFeedbackFixture F;
    if(!TestTrue(TEXT("Two already-owned durable claims"),F.Init({Alice,Bob},W))){AddError(F.Why);return false;}
    auto& A=F.Players[0];auto& B=F.Players[1];const FGuid OldBobOrigin=FGuid::NewGuid(),NewBobOrigin=FGuid::NewGuid();
    F.Store->HoldNextSnapshot={TEXT("Alice"),TEXT("Bob")};
    if(!TestTrue(TEXT("Queue Alice completion"),F.Runtime->SubmitLootClaim(A.Controller,LA.ClaimId,FGuid::NewGuid(),F.Why))||
        !TestTrue(TEXT("Queue Bob completion"),F.Runtime->SubmitLootClaim(B.Controller,LB.ClaimId,OldBobOrigin,F.Why))||
        !TestTrue(TEXT("Both exact real snapshots ready for one Poll batch"),PumpLootRuntime(*F.Runtime,[&]{return F.Store->CapturedSnapshots(2);})))return false;
    bool Reentered=false,Accepted=false;FString ReentryReason;
    ON_SCOPE_EXIT {if(F.Runtime)F.Runtime->SetWorldPublisher({});};
    F.Runtime->SetWorldPublisher([&](const auto&)
    {
        if(Reentered)return;Reentered=true;
        Accepted=F.Runtime->SubmitLootClaim(B.Controller,LB.ClaimId,NewBobOrigin,ReentryReason);
    });
    F.Store->ReleaseSnapshots();F.Runtime->Tick(.04f);
    TestTrue(TEXT("Actual PublishWorld callback reentered"),Reentered);
    if(!TestTrue(TEXT("Whole old batch was detached before first callback; new Bob origin accepted"),Accepted)){AddError(ReentryReason);return false;}
    TestEqual(TEXT("Bob's old batch result still reaches its own correlation"),B.Pawn->Feedback,FString(LootMessage(EAetherLootClaimOutcome::AlreadyOwned)));
    TestTrue(TEXT("Reentrant new request remains pending after old batch dispatch"),F.Runtime->Inspect().PendingFacts>0||F.Runtime->Inspect().DeferredFacts>0);
    B.Pawn->Feedback=TEXT("Waiting for the reentrant origin");
    if(!TestTrue(TEXT("Second origin completes independently"),PumpLootRuntime(*F.Runtime,[&]{return F.Ready();})))return false;
    TestEqual(TEXT("Old completion did not consume newer request association"),B.Pawn->Feedback,FString(LootMessage(EAetherLootClaimOutcome::AlreadyOwned)));
    int32 Presentations=0;const auto Handle=B.Pawn->OnPresentationChanged.AddLambda([&]{++Presentations;});
    B.Controller->ClientV10LootClaimResult_Implementation(B.Client->GetChannel(),OldBobOrigin,LB.ClaimId,EAetherLootClaimOutcome::AlreadyOwned);
    B.Controller->ClientV10LootClaimResult_Implementation(B.Client->GetChannel(),NewBobOrigin,LB.ClaimId,EAetherLootClaimOutcome::AlreadyOwned);
    TestEqual(TEXT("Both delivered origins are independently deduplicated"),Presentations,0);B.Pawn->OnPresentationChanged.Remove(Handle);
    FAetherProfileStateV10 Disk;if(!TestTrue(TEXT("Read final real Bob aggregate"),F.ReadProfile(TEXT("Bob"),Disk)))return false;
    TestEqual(TEXT("Batch reentry cannot reaward previously claimed loot"),LootItemCount(Disk,TEXT("IronCuirass")),1);
    TestEqual(TEXT("AlreadyOwned batch and reentry perform no writes"),F.Store->Commits,0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherLootClaimTypedCompletionTest,"Aether.V10.Network.LootClaimExactPendingIdentityAndTypedCompletion",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherLootClaimTypedCompletionTest::RunTest(const FString&)
{
    FAetherWorldStateV10 W;const auto Loot=MakeLoot();W.Loot.Add(Loot);FLootFeedbackFixture F;
    if(!TestTrue(TEXT("Real typed-result fixture"),F.Init({MakeLootProfile(TEXT("Alice")),MakeLootProfile(TEXT("Bob"))},W))){AddError(F.Why);return false;}
    FAetherServerFactCoordinator Facts(F.Store.ToSharedRef());FAetherServerFact E;
    E.Kind=EAetherServerFactKind::LegacyLoot;E.CharacterId=TEXT("Alice");E.FactId=TEXT("Loot");E.InstanceId=Loot.ClaimId;
    if(!TestTrue(TEXT("Trusted Alice fact accepted"),Facts.Enqueue(E,F.Why)))return false;
    TestTrue(TEXT("Exact character plus instance is pending"),Facts.HasPendingLootClaim(TEXT("Alice"),Loot.ClaimId));
    TestFalse(TEXT("Character matching is case-sensitive"),Facts.HasPendingLootClaim(TEXT("alice"),Loot.ClaimId));
    TestFalse(TEXT("Same instance does not block a different character"),Facts.HasPendingLootClaim(TEXT("Bob"),Loot.ClaimId));
    const FGuid Missing=FGuid::NewGuid();TestFalse(TEXT("Same character does not block a different instance"),Facts.HasPendingLootClaim(TEXT("Alice"),Missing));
    E.InstanceId=Missing;if(!TestTrue(TEXT("Different Alice instance accepted"),Facts.Enqueue(E,F.Why)))return false;
    E.CharacterId=TEXT("Bob");E.InstanceId=Loot.ClaimId;if(!TestTrue(TEXT("Independent Bob claim accepted"),Facts.Enqueue(E,F.Why)))return false;
    const auto DrainFacts=[&](TArray<FAetherServerFactCompletion>& Out)
    {
        const double End=FPlatformTime::Seconds()+5;
        while(Facts.PendingCount()&&FPlatformTime::Seconds()<End)
        {Out.Append(Facts.Poll(FPlatformTime::Seconds()));FPlatformProcess::Sleep(.001f);}
        return Facts.PendingCount()==0;
    };
    TArray<FAetherServerFactCompletion> Done;
    if(!TestTrue(TEXT("All typed outcomes resolve using SQLite"),DrainFacts(Done))||!TestEqual(TEXT("One completion per distinct identity"),Done.Num(),3))return false;
    int32 Applied=0,Other=0,Absent=0;
    for(const auto& C:Done)
    {
        if(!TestTrue(TEXT("LegacyLoot terminal completion carries typed outcome"),C.LootOutcome.IsSet()))return false;
        Applied+=C.LootOutcome.GetValue()==EAetherLootClaimOutcome::Applied?1:0;
        Other+=C.LootOutcome.GetValue()==EAetherLootClaimOutcome::ClaimedByOther?1:0;
        Absent+=C.LootOutcome.GetValue()==EAetherLootClaimOutcome::Missing?1:0;
        if(C.LootOutcome.GetValue()==EAetherLootClaimOutcome::Applied)
            TestTrue(TEXT("Applied typed outcome has committed store proof and verified snapshot"),C.Code==EAetherStoreCode::Committed&&C.Profile.IsSet()&&C.World.IsSet());
    }
    TestTrue(TEXT("Exactly one durable winner, one competing loser and one missing instance"),Applied==1&&Other==1&&Absent==1);
    TestFalse(TEXT("Completed identity is released"),Facts.HasPendingLootClaim(TEXT("Alice"),Loot.ClaimId));
    E={};E.Kind=EAetherServerFactKind::Settle;E.CharacterId=TEXT("Alice");Done.Reset();
    if(!TestTrue(TEXT("Ordinary Settle accepted"),Facts.Enqueue(E,F.Why))||!TestTrue(TEXT("Ordinary Settle completes"),DrainFacts(Done))||
        !TestEqual(TEXT("One nonloot completion"),Done.Num(),1))return false;
    TestFalse(TEXT("Nonloot facts never acquire loot feedback semantics"),Done[0].LootOutcome.IsSet());
    E.Kind=EAetherServerFactKind::LegacyLoot;E.FactId=TEXT("Loot");E.InstanceId=FGuid::NewGuid();
    F.Store->CorruptNextSnapshot.Add(TEXT("Alice"));Done.Reset();
    if(!TestTrue(TEXT("Read-failure claim accepted"),Facts.Enqueue(E,F.Why))||!TestTrue(TEXT("Corrupt initial read resolves"),DrainFacts(Done))||
        !TestEqual(TEXT("One corrupt-read completion"),Done.Num(),1))return false;
    TestTrue(TEXT("Pre-commit corrupt read cannot invent Applied or a candidate snapshot"),Done[0].LootOutcome.IsSet()&&
        Done[0].LootOutcome.GetValue()==EAetherLootClaimOutcome::Invalid&&!Done[0].Profile.IsSet()&&!Done[0].World.IsSet());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherLootClaimControllerIdentityTest,"Aether.V10.Network.LootClaimRejectsStillLiveReplacedController",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherLootClaimControllerIdentityTest::RunTest(const FString&)
{
    FAetherWorldStateV10 W;FLootFeedbackFixture F;
    if(!TestTrue(TEXT("Ready controller ownership fixture"),F.Init({MakeLootProfile(TEXT("Alice"))},W))){AddError(F.Why);return false;}
    auto& A=F.Players[0];auto* Local=A.Controller->GetLocalPlayer();auto* NewController=F.World->SpawnActor<AAetherPlayerController>();
    if(!TestNotNull(TEXT("Actual replacement controller"),NewController)||!TestNotNull(TEXT("Existing local player"),Local))return false;
    const FGuid Channel=FGuid::NewGuid();const FString Sentinel=TEXT("Old controller must stay silent");A.Pawn->Feedback=Sentinel;
    NewController->SetPlayer(Local);A.Client->ReceiveChannel(NewController,Channel,TEXT("Alice"),F.InitialWorld.RealmId);
    TestTrue(TEXT("New controller owns LocalPlayer before old actor destruction"),IsValid(A.Controller)&&Local->GetPlayerController(F.World)==NewController);
    TestTrue(TEXT("Packet channel itself is valid for the new transport"),A.Client->GetChannel()==Channel);
    TestFalse(TEXT("Old live controller fails the independent owner-identity gate"),A.Client->AcceptsControllerIdentity(A.Controller));
    int32 Presentations=0;const auto Handle=A.Pawn->OnPresentationChanged.AddLambda([&]{++Presentations;});
    A.Controller->ClientV10LootClaimResult_Implementation(Channel,FGuid::NewGuid(),FGuid::NewGuid(),EAetherLootClaimOutcome::Applied);
    TestEqual(TEXT("Matching channel cannot authorize a replaced controller"),A.Pawn->Feedback,Sentinel);
    TestEqual(TEXT("Rejected controller packet does not broadcast UI changes"),Presentations,0);A.Pawn->OnPresentationChanged.Remove(Handle);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherLootClaimPendingBoundsTest,"Aether.V10.Network.LootClaimBoundedCorrelationsAndDeferredDuplicateIdentity",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherLootClaimPendingBoundsTest::RunTest(const FString&)
{
    for(const bool Deferred:{false,true})
    {
        // Historical claimed loot can legitimately outlive inventory items which
        // were later sold. AlreadyOwned gives 128 real read-only completions,
        // without manufacturing 128 rewards or filling the log with failures.
        FAetherWorldStateV10 W;for(int32 I=0;I<128;++I)W.Loot.Add(MakeLoot(TEXT("Alice")));
        FLootFeedbackFixture F;
        if(!TestTrue(TEXT("Bounded-queue fixture with real historical ownership"),F.Init({MakeLootProfile(TEXT("Alice")),MakeLootProfile(TEXT("Bob"))},W))){AddError(F.Why);return false;}
        auto& A=F.Players[0];auto& B=F.Players[1];const FGuid FirstOrigin=FGuid::NewGuid(),BobOrigin=FGuid::NewGuid();
        for(int32 I=0;I<128;++I)
        {
            if(Deferred)
            {
                FAetherServerFact E;E.Kind=EAetherServerFactKind::LegacyLoot;E.CharacterId=TEXT("Alice");E.FactId=TEXT("Loot");E.InstanceId=W.Loot[I].ClaimId;
                if(!TestTrue(TEXT("Fill native fact queue with distinct exact identities"),F.Runtime->ObserveServerFact(E,F.Why)))return false;
            }
            else if(!TestTrue(TEXT("Same character may queue another distinct instance"),
                F.Runtime->SubmitLootClaim(A.Controller,W.Loot[I].ClaimId,I==0?FirstOrigin:FGuid::NewGuid(),F.Why)))return false;
        }
        TestEqual(TEXT("Native coordinator has bounded 128-job queue"),F.Runtime->Inspect().PendingFacts,128);
        TestFalse(TEXT("Exact duplicate in Facts is rejected rather than replacing its origin"),
            F.Runtime->SubmitLootClaim(A.Controller,W.Loot[0].ClaimId,FGuid::NewGuid(),F.Why));
        if(Deferred)
        {
            if(!TestTrue(TEXT("Another character's same loot claim enters Deferred"),F.Runtime->SubmitLootClaim(B.Controller,W.Loot[0].ClaimId,BobOrigin,F.Why)))return false;
            TestEqual(TEXT("One retained deferred claim"),F.Runtime->Inspect().DeferredFacts,1);
            TestFalse(TEXT("Exact duplicate in Deferred cannot overwrite Bob's original origin"),
                F.Runtime->SubmitLootClaim(B.Controller,W.Loot[0].ClaimId,FGuid::NewGuid(),F.Why));
            TestEqual(TEXT("Rejected deferred duplicate creates no extra retained work"),F.Runtime->Inspect().DeferredFacts,1);
        }
        else
        {
            TestFalse(TEXT("129th distinct correlation is explicitly rejected"),F.Runtime->SubmitLootClaim(A.Controller,FGuid::NewGuid(),FGuid::NewGuid(),F.Why));
            TestEqual(TEXT("Association backpressure never silently accepts deferred work"),F.Runtime->Inspect().DeferredFacts,0);
        }
        if(!TestTrue(TEXT("All accepted facts reach terminal real-store completions"),PumpLootRuntime(*F.Runtime,[&]{return F.Ready();})))return false;
        auto& Owner=Deferred?B:A;int32 Presentations=0;
        const auto Handle=Owner.Pawn->OnPresentationChanged.AddLambda([&]{++Presentations;});
        Owner.Controller->ClientV10LootClaimResult_Implementation(Owner.Client->GetChannel(),Deferred?BobOrigin:FirstOrigin,W.Loot[0].ClaimId,
            Deferred?EAetherLootClaimOutcome::ClaimedByOther:EAetherLootClaimOutcome::AlreadyOwned);
        TestEqual(TEXT("Original accepted origin was delivered and retained for deduplication"),Presentations,0);Owner.Pawn->OnPresentationChanged.Remove(Handle);
        TestEqual(TEXT("Queue draining preserves final ownership message"),Owner.Pawn->Feedback,
            FString(LootMessage(Deferred?EAetherLootClaimOutcome::ClaimedByOther:EAetherLootClaimOutcome::AlreadyOwned)));
        TestEqual(TEXT("Backpressure and duplicate requests never create rewards"),F.Store->Commits,0);
    }
    return true;
}
#endif
