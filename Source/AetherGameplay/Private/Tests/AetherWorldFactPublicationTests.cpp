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
// All reads, receipts and transactions use real SQLite. The wrapper can hold one
// completed Bob snapshot to deterministically exercise the old-Settle race.
class FWorldFactPublicationStore final : public IAetherTransactionalStore
{
public:
    explicit FWorldFactPublicationStore(TSharedRef<IAetherTransactionalStore,ESPMode::ThreadSafe> In):Inner(MoveTemp(In)){}
    TSharedRef<IAetherTransactionalStore,ESPMode::ThreadSafe> Inner;
    TMap<FString,int32> SnapshotReads;
    bool bHoldBobRead=false;
    TFuture<FAetherStoreSnapshotResult> HeldRead;
    TUniquePtr<TPromise<FAetherStoreSnapshotResult>> HeldReply;
    TOptional<FAetherStoreSnapshotResult> HeldSnapshot;
    virtual TFuture<FAetherStoreSnapshotResult> ReadSnapshot(FAetherStoreSnapshotQuery Q) override
    {
        bool Bob=false;
        for(const auto& Key:Q.Keys)if(Key.Kind==EAetherAggregateKind::Profile)
        {++SnapshotReads.FindOrAdd(Key.Id);Bob|=Key.Id==TEXT("Bob");}
        if(Bob&&bHoldBobRead&&!HeldReply)
        {
            bHoldBobRead=false;HeldRead=Inner->ReadSnapshot(MoveTemp(Q));
            HeldReply=MakeUnique<TPromise<FAetherStoreSnapshotResult>>();return HeldReply->GetFuture();
        }
        return Inner->ReadSnapshot(MoveTemp(Q));
    }
    bool CaptureHeldRead()
    {
        if(!HeldRead.IsValid()||!HeldRead.IsReady())return false;
        HeldSnapshot=HeldRead.Get();HeldRead={};return true;
    }
    void ReleaseHeldRead()
    {
        if(!HeldReply)return;
        if(!HeldSnapshot.IsSet()&&HeldRead.IsValid()){HeldSnapshot=HeldRead.Get();HeldRead={};}
        HeldReply->SetValue(HeldSnapshot.IsSet()?MoveTemp(HeldSnapshot.GetValue()):FAetherStoreSnapshotResult());
        HeldReply.Reset();HeldSnapshot.Reset();
    }
    virtual TFuture<FAetherStoreResult> Commit(FAetherTransaction T) override {return Inner->Commit(MoveTemp(T));}
    virtual TFuture<FAetherStoreResult> LookupReceipt(FAetherReceiptQuery Q) override {return Inner->LookupReceipt(MoveTemp(Q));}
    virtual TFuture<FAetherStoreReadResult> Read(FAetherAggregateKey K) override {return Inner->Read(MoveTemp(K));}
    virtual TFuture<FAetherStoreRevisionIndex> ReadRevisions(EAetherAggregateKind K) override {return Inner->ReadRevisions(K);}
    virtual TFuture<FAetherStoreEffectsResult> PendingEffects(FString Id) override {return Inner->PendingEffects(MoveTemp(Id));}
    virtual TFuture<bool> AcknowledgeEffect(FString Id,FGuid Delivery) override {return Inner->AcknowledgeEffect(MoveTemp(Id),Delivery);}
    virtual TFuture<bool> Backup(FString Path) override {return Inner->Backup(MoveTemp(Path));}
    virtual void Close() override {ReleaseHeldRead();Inner->Close();}
    virtual ~FWorldFactPublicationStore() override {Close();}
};
struct FWorldFactPlayer
{
    AAetherPlayerController* Controller=nullptr;
    AAetherPlayerState* State=nullptr;
    AAetherFrontierCharacter* Pawn=nullptr;
    UAetherCommandClient* Client=nullptr;
};
FWorldFactPlayer MakeWorldFactPlayer(UGameInstance& GI,int32 Index,const FString& Id,FString& Why)
{
    FWorldFactPlayer P;auto* Local=GI.CreateLocalPlayer(Index,Why,false);if(!Local)return P;
    auto* W=GI.GetWorld();FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    P.Controller=W->SpawnActor<AAetherPlayerController>();P.State=W->SpawnActor<AAetherPlayerState>();
    P.Pawn=W->SpawnActor<AAetherFrontierCharacter>(FVector(Index*400,0,0),FRotator::ZeroRotator,Params);
    if(!P.Controller||!P.State||!P.Pawn)return P;
    P.State->Profile.CharacterId=Id;P.State->SetOwner(P.Controller);P.Controller->PlayerState=P.State;
    P.State->AbilitySystem->AddAttributeSetSubobject(P.State->Attributes.Get());
    // The profiles have no equipped slots. A valid invisible catalog entry keeps
    // the actual equipment/ASC publication path active without loading art assets.
    auto* Catalog=NewObject<UAetherEquipmentCatalog>(P.Pawn);
    auto* Item=NewObject<UAetherEquipmentDefinition>(Catalog);Item->ItemId="FixtureAccessory";Item->Slot="Neck";Item->bInvisibleAccessory=true;
    Catalog->Items.Add(Item);P.Pawn->Equipment->Catalog=Catalog;
    P.Controller->SetPlayer(Local);P.Controller->Possess(P.Pawn);
    P.Pawn->SetPlayerState(P.State);P.Pawn->BindPersistentAbilities();
    P.Client=Local->GetSubsystem<UAetherCommandClient>();return P;
}
bool ReplaceWorldFactPawn(FWorldFactPlayer& P)
{
    if(!P.Controller||!P.State||!P.Pawn)return false;
    auto* W=P.Controller->GetWorld();auto* Catalog=P.Pawn->Equipment->Catalog.Get();
    P.Controller->UnPossess();FActorSpawnParameters Spawn;
    Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Replacement=W->SpawnActor<AAetherFrontierCharacter>(FVector(800,0,0),FRotator::ZeroRotator,Spawn);
    if(!Replacement)return false;
    Replacement->Equipment->Catalog=Catalog;P.Pawn=Replacement;
    P.Controller->Possess(P.Pawn);P.Pawn->SetPlayerState(P.State);P.Pawn->BindPersistentAbilities();return true;
}
bool PumpRuntimeUntil(UAetherCommandRuntime& Runtime,TFunctionRef<bool()> Done)
{
    const double End=FPlatformTime::Seconds()+5;
    do {Runtime.Tick(.04f);if(Done())return true;FPlatformProcess::Sleep(.001f);}while(FPlatformTime::Seconds()<End);
    return false;
}
bool RuntimeIdle(const UAetherCommandRuntime& Runtime)
{
    const auto M=Runtime.Inspect();return M.PendingCommands==0&&M.PendingFacts==0&&M.DeferredFacts==0;
}
int32 ItemCount(const FAetherProfileStateV10& P,const FString& Id)
{int32 Count=0;for(const auto& Item:P.Inventory.Items)if(Item.DefinitionId==Id)Count+=Item.Quantity;return Count;}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherWorldFactPublicationTest,"Aether.V10.Network.CommittedWorldFactsSettleIdleOnlinePlayers",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherWorldFactPublicationTest::RunTest(const FString&)
{
    const auto& D=FAetherV10Definitions::Get();FString Why;
    if(!TestTrue(TEXT("Definitions ready"),D.bValid))return false;
    // Run once normally, then with Bob already holding a Settle read from before
    // Alice's transaction. Bob never submits a player command in either case.
    for(const bool HoldOldSettle:{false,true})
    {
        FAetherWorldStateV10 InitialWorld;InitialWorld.RealmId=FGuid::NewGuid();
        InitialWorld.WorldFactSources.Add(TEXT("ForestFire0"),TEXT("ForestFire0"));
        FAetherProfileStateV10 Alice,Bob;Alice.CharacterId=TEXT("Alice");Bob.CharacterId=TEXT("Bob");
        for(auto* P:{&Alice,&Bob})
        {
            P->Inventory.Capacity=D.Items.DefaultCapacity;P->Claims={TEXT("Q_Main_01"),TEXT("Q_Main_02"),TEXT("Q_Main_03")};
            P->Gold=123;P->Experience=350;P->bRegistered=true;
            // Fixture history is already settled against the activation baseline.
            const auto Settled=AetherQuestProgression::Settle(*P,InitialWorld.WorldFactSources,{},D.Items,D.Skills,D.Rules,D.Progression);
            if(!TestTrue(TEXT("Historical fact fixture valid"),Settled.Code==EAetherQuestMutationCode::Applied||Settled.Code==EAetherQuestMutationCode::Unchanged))return false;
        }
        FAetherSqliteOptions O;O.DatabasePath=FPaths::ProjectSavedDir()/TEXT("Automation/WorldFactPublication")/FGuid::NewGuid().ToString(EGuidFormats::Digits)/TEXT("state.sqlite");
        O.Fault=MakeShared<TAtomic<EAetherStoreFault>,ESPMode::ThreadSafe>(EAetherStoreFault::None);
        auto Open=AetherSQLite::Open(O);if(!TestTrue(TEXT("Isolated real SQLite store"),Open.Store.IsValid()))return false;
        auto Store=MakeShared<FWorldFactPublicationStore,ESPMode::ThreadSafe>(Open.Store.ToSharedRef());
        FAetherTransaction Seed;Seed.ActorId=Alice.CharacterId;Seed.ExpectedProfileRevision=-1;
        Seed.CommandId=AetherTransactions::NewCommandId(-1);Seed.Request={1};
        for(const auto& P:{Alice,Bob})
        {
            FAetherAggregateWrite Row;Row.Value.Key={EAetherAggregateKind::Profile,P.CharacterId};
            if(!TestTrue(TEXT("Encode native fixture"),AetherProfileCodec::Encode(P,D.Items,D.Skills,D.Rules,Row.Value.Payload,Why)))return false;
            Seed.Writes.Add(MoveTemp(Row));
        }
        FAetherAggregateWrite WorldRow;WorldRow.Value.Key={EAetherAggregateKind::World,TEXT("Main")};
        if(!TestTrue(TEXT("Encode audited world"),AetherWorldCodec::Encode(InitialWorld,D.Items,D.Rules,{{TEXT("Alice"),0},{TEXT("Bob"),0}},WorldRow.Value.Payload,Why)))return false;
        Seed.Writes.Add(MoveTemp(WorldRow));
        if(!TestTrue(TEXT("Seed is one committed transaction"),Store->Commit(MoveTemp(Seed)).Get().Code==EAetherStoreCode::Committed))return false;
        auto* GI=NewObject<UGameInstance>(GEngine);GI->AddToRoot();GI->InitializeStandalone(NAME_None,nullptr);
        auto* W=GI->GetWorld();auto* Runtime=GI->GetSubsystem<UAetherCommandRuntime>();
        ON_SCOPE_EXIT
        {
            Store->ReleaseHeldRead();if(Runtime)Runtime->DrainBackend();GI->Shutdown();
            if(W){GEngine->DestroyWorldContext(W);W->DestroyWorld(false);}GI->RemoveFromRoot();Store->Close();
        };
        if(!TestNotNull(TEXT("Isolated scene"),W)||!TestNotNull(TEXT("Production runtime"),Runtime))return false;
        FAetherProfileCommandContext Context;Context.bCanManageInventory=true;Context.bServiceRequirementsMet=true;
        auto& Target=Context.Interaction;Target.DefinitionId=TEXT("SupplyRestored");Target.TargetStableId=TEXT("Pump");Target.InteractionRevision=7;
        Target.bLoaded=Target.bInRange=Target.bLineOfSight=Target.bActorCanAct=true;
        int32 AliceResolves=0,BobResolves=0;
        const FAetherResolveConnectedContext Resolve=[&](auto& PC,const auto&,const auto&,auto& Out)
        {if(PC.template GetPlayerState<AAetherPlayerState>()->Profile.CharacterId==TEXT("Bob"))++BobResolves;else ++AliceResolves;Out=Context;return true;};
        const FAetherPublishConnectedState Publish=[](auto& PC,const auto& P,const auto*,const auto*)
        {FString Reason;return PC.template GetPlayerState<AAetherPlayerState>()->PublishNativeProfile(P,Reason);};
        auto InvalidBaseline=InitialWorld;InvalidBaseline.Revision=-1;
        TestFalse(TEXT("Negative baseline cannot install a backend"),Runtime->InstallBackend(Store,InvalidBaseline,Resolve,Publish,Why));
        InvalidBaseline.Revision=MAX_int64;
        TestFalse(TEXT("Exhausted baseline cannot install a backend"),Runtime->InstallBackend(Store,InvalidBaseline,Resolve,Publish,Why));
        TestFalse(TEXT("Rejected baseline leaves no partial backend"),Runtime->HasBackend());
        if(!TestTrue(TEXT("Install from audited activation snapshot"),Runtime->InstallBackend(Store,InitialWorld,Resolve,Publish,Why)))return false;
        TestTrue(TEXT("Same realm confirmation stays idempotent"),Runtime->SetBackendDomain(InitialWorld.RealmId)&&Runtime->SetBackendDomain(InitialWorld.RealmId));
        TestFalse(TEXT("Another realm cannot reuse baseline before login"),Runtime->SetBackendDomain(FGuid::NewGuid()));
        auto A=MakeWorldFactPlayer(*GI,0,Alice.CharacterId,Why),B=MakeWorldFactPlayer(*GI,1,Bob.CharacterId,Why);
        if(!TestNotNull(TEXT("Alice client"),A.Client)||!TestNotNull(TEXT("Bob client"),B.Client)||
            !TestTrue(TEXT("Two authenticated bindings"),Runtime->BindVerifiedPlayer(A.Controller,Alice.CharacterId)&&Runtime->BindVerifiedPlayer(B.Controller,Bob.CharacterId)))return false;
        const auto Ready=[&]
        {return RuntimeIdle(*Runtime)&&A.Client->GetProfile().IsSet()&&B.Client->GetProfile().IsSet()&&
            A.Pawn->ResourceGate->IsPresentationSettled()&&B.Pawn->ResourceGate->IsPresentationSettled();};
        if(!TestTrue(TEXT("Actual channels, profiles and ASC recovery are ready"),PumpRuntimeUntil(*Runtime,Ready)))return false;
        TestEqual(TEXT("Historical baseline does not fan out another Bob Settle"),Store->SnapshotReads.FindRef(TEXT("Bob")),1);
        TestEqual(TEXT("Initial binding does not rewrite already settled Bob"),B.Client->GetProfile()->Revision,int64(0));
        // Altering the caller's value after installation cannot change the copied
        // baseline and suppress publication of the later real water fact.
        InitialWorld.WorldFactSources.Add(TEXT("SupplyRestored"),TEXT("Pump"));
        const auto ReadProfile=[&](const FString& Id,FAetherProfileStateV10& P)
        {
            const auto R=Store->Read({EAetherAggregateKind::Profile,Id}).Get();
            return R.Code==EAetherStoreCode::Found&&R.Value.IsSet()&&AetherProfileCodec::Decode(R.Value->Payload,D.Items,D.Skills,D.Rules,P,Why);
        };
        const auto ReadWorld=[&](FAetherWorldStateV10& World)
        {
            FAetherStoreSnapshotQuery Q;Q.Keys={{EAetherAggregateKind::World,TEXT("Main")}};Q.bIncludeProfileRevisions=true;
            const auto R=Store->ReadSnapshot(MoveTemp(Q)).Get();const auto* Row=R.Values.Find({EAetherAggregateKind::World,TEXT("Main")});
            return R.Code==EAetherStoreCode::Found&&Row&&AetherWorldCodec::Decode(Row->Payload,D.Items,D.Rules,R.ProfileRevisions,World,Why);
        };
        FAetherPlayerCommand Pump;Pump.Type=EAetherCommandType::ExecuteInteraction;Pump.ProtocolVersion=AetherCommands::LatestProtocolVersion;
        Pump.CommandId=AetherTransactions::NewCommandId(0);Pump.ExpectedProfileRevision=0;Pump.ExpectedWorldRevision=0;
        Pump.TargetStableId=TEXT("Pump");Pump.ActionId=TEXT("Pump");Pump.ExpectedInteractionRevision=7;
        FAetherV10CommandPacket Packet;Packet.Channel=A.Client->GetChannel();
        if(!TestTrue(TEXT("Freeze real interaction packet"),AetherCommands::Encode(Pump,Packet.Bytes,Why)))return false;
        int32 BobReads=Store->SnapshotReads.FindRef(TEXT("Bob"));const auto BobBefore=B.Client->GetProfile().GetValue();
        O.Fault->Store(EAetherStoreFault::AfterFirstWrite);Runtime->Receive(A.Controller,Packet);
        if(!TestTrue(TEXT("Failed command reaches a terminal completion"),PumpRuntimeUntil(*Runtime,[&]{return RuntimeIdle(*Runtime);})))return false;
        TestTrue(TEXT("Failure injection reached actual SQLite transaction"),O.Fault->Load()==EAetherStoreFault::None);
        FAetherWorldStateV10 DiskWorld;FAetherProfileStateV10 DiskBob;
        if(!TestTrue(TEXT("Read rolled back world and peer"),ReadWorld(DiskWorld)&&ReadProfile(TEXT("Bob"),DiskBob)))return false;
        TestFalse(TEXT("Failed write never publishes world fact"),DiskWorld.WorldFactSources.Contains(TEXT("SupplyRestored")));
        TestEqual(TEXT("Failed write never enqueues peer settlement"),Store->SnapshotReads.FindRef(TEXT("Bob")),BobReads);
        TestEqual(TEXT("Failed write leaves peer revision unchanged"),DiskBob.Revision,BobBefore.Revision);
        if(HoldOldSettle)
        {
            Store->bHoldBobRead=true;FAetherServerFact Old;Old.Kind=EAetherServerFactKind::Settle;Old.CharacterId=TEXT("Bob");
            if(!TestTrue(TEXT("Server recovery Settle accepted"),Runtime->ObserveServerFact(Old,Why))||
                !TestTrue(TEXT("Bob has read the old committed world"),PumpRuntimeUntil(*Runtime,[&]{return Store->CaptureHeldRead();})))return false;
        }
        bool PublishedWater=false;Runtime->SetWorldPublisher([&](const auto& World){PublishedWater|=World.WorldFactSources.Contains(TEXT("SupplyRestored"));});
        Runtime->Receive(A.Controller,Packet);
        if(!TestTrue(TEXT("Alice's real command completion publishes water fact"),PumpRuntimeUntil(*Runtime,[&]{return PublishedWater;})))return false;
        if(HoldOldSettle)
        {
            if(!TestTrue(TEXT("Read Bob while old Settle is held"),ReadProfile(TEXT("Bob"),DiskBob)))return false;
            TestFalse(TEXT("Peer cannot gain a reward from an uncommitted candidate"),DiskBob.Claims.Contains(TEXT("Q_Main_05")));
            Store->ReleaseHeldRead();
        }
        if(!TestTrue(TEXT("Idle Bob automatically gets durable reward and owner snapshot"),PumpRuntimeUntil(*Runtime,[&]
            {return RuntimeIdle(*Runtime)&&B.Client->GetProfile().IsSet()&&B.Client->GetProfile()->Claims.Contains(TEXT("Q_Main_05"));})))return false;
        if(!TestTrue(TEXT("Peer progression is read back from SQLite"),ReadProfile(TEXT("Bob"),DiskBob)&&ReadWorld(DiskWorld)))return false;
        TestTrue(TEXT("World evidence and quest claim are durable"),DiskWorld.bSupplyRestored&&DiskBob.Evidence.Contains(TEXT("SupplyRestored"))&&DiskBob.Claims.Contains(TEXT("Q_Main_05")));
        TestEqual(TEXT("Peer receives exactly one quest gold reward"),DiskBob.Gold,BobBefore.Gold+60);
        TestEqual(TEXT("Peer receives exactly one quest XP reward"),DiskBob.Experience,BobBefore.Experience+100);
        TestEqual(TEXT("Peer receives exactly one quest item"),ItemCount(DiskBob,TEXT("TideStaff")),1);
        TestEqual(TEXT("Peer receives exactly one quest point source"),DiskBob.Skills.AvailableSkillPoints,BobBefore.Skills.AvailableSkillPoints+2);
        TestEqual(TEXT("Bob never submitted or resolved a player command"),BobResolves,0);
        const auto RewardedBob=DiskBob;BobReads=Store->SnapshotReads.FindRef(TEXT("Bob"));const int32 ResolvedBeforeReplay=AliceResolves;
        Runtime->Receive(A.Controller,Packet);
        if(!TestTrue(TEXT("Original fixed request replays through runtime"),PumpRuntimeUntil(*Runtime,[&]{return RuntimeIdle(*Runtime);})))return false;
        TestEqual(TEXT("Receipt replay does not rebuild candidate"),AliceResolves,ResolvedBeforeReplay);
        TestEqual(TEXT("Receipt replay does not fan out peer settlement"),Store->SnapshotReads.FindRef(TEXT("Bob")),BobReads);
        if(!TestTrue(TEXT("Replayed reward read back"),ReadProfile(TEXT("Bob"),DiskBob)))return false;
        TestTrue(TEXT("Replay preserves revision and all rewards"),DiskBob.Revision==RewardedBob.Revision&&DiskBob.Gold==RewardedBob.Gold&&
            DiskBob.Experience==RewardedBob.Experience&&DiskBob.Skills.PointEvents.Num()==RewardedBob.Skills.PointEvents.Num()&&ItemCount(DiskBob,TEXT("TideStaff"))==1);
        // Duplicate world observations and a personal skill-point interaction
        // must not launch an all-player broadcast just because revision advances.
        FAetherServerFact Duplicate;Duplicate.Kind=EAetherServerFactKind::World;Duplicate.CharacterId=TEXT("Alice");
        Duplicate.FactId=TEXT("SupplyRestored");Duplicate.SourceId=TEXT("Pump");
        if(!TestTrue(TEXT("Repeated trusted world event accepted"),Runtime->ObserveServerFact(Duplicate,Why))||
            !TestTrue(TEXT("Repeated trusted world event drains"),PumpRuntimeUntil(*Runtime,[&]{return RuntimeIdle(*Runtime);})))return false;
        TestEqual(TEXT("Duplicate server world event does not broadcast"),Store->SnapshotReads.FindRef(TEXT("Bob")),BobReads);
        FAetherProfileStateV10 CurrentAlice;if(!TestTrue(TEXT("Read current actor"),ReadProfile(TEXT("Alice"),CurrentAlice)&&ReadWorld(DiskWorld)))return false;
        Context.Interaction.DefinitionId=TEXT("Teacher");Context.Interaction.TargetStableId=TEXT("Town.Teacher");Context.Interaction.InteractionRevision=9;
        FAetherPlayerCommand Points=Pump;Points.CommandId=AetherTransactions::NewCommandId(CurrentAlice.Revision);Points.ExpectedProfileRevision=CurrentAlice.Revision;
        Points.ExpectedWorldRevision=DiskWorld.Revision;Points.ExpectedInteractionRevision=9;Points.TargetStableId=TEXT("Town.Teacher");Points.ActionId=TEXT("ClaimSkillPoints");
        if(!TestTrue(TEXT("Encode non-world-fact interaction"),AetherCommands::Encode(Points,Packet.Bytes,Why)))return false;
        Runtime->Receive(A.Controller,Packet);
        if(!TestTrue(TEXT("Non-fact world revision change completes"),PumpRuntimeUntil(*Runtime,[&]{return RuntimeIdle(*Runtime);})))return false;
        FAetherWorldStateV10 AfterPoints;if(!TestTrue(TEXT("Read later world"),ReadWorld(AfterPoints)))return false;
        TestTrue(TEXT("Personal points really advanced world revision"),AfterPoints.Revision>DiskWorld.Revision);
        TestEqual(TEXT("Unchanged world facts do not re-settle Bob"),Store->SnapshotReads.FindRef(TEXT("Bob")),BobReads);
        // New trusted fact still uses the same publication path as the interaction.
        // In the raced scenario it arrives during Bob's real no-Pawn interval.
        if(HoldOldSettle)B.Controller->UnPossess();
        FAetherServerFact Fire;Fire.Kind=EAetherServerFactKind::World;Fire.CharacterId=TEXT("Alice");Fire.FactId=TEXT("ForestFire1");Fire.SourceId=TEXT("ForestFire1");
        if(!TestTrue(TEXT("New trusted world event accepted"),Runtime->ObserveServerFact(Fire,Why)))return false;
        if(HoldOldSettle)
        {
            if(!TestTrue(TEXT("World fact commits during Bob's no-Pawn gap"),PumpRuntimeUntil(*Runtime,[&]
                {return RuntimeIdle(*Runtime)&&A.Client->GetProfile()->Evidence.Contains(TEXT("ForestFire1"));})))return false;
            if(!TestTrue(TEXT("Read peer during avatar gap"),ReadProfile(TEXT("Bob"),DiskBob)))return false;
            TestFalse(TEXT("No stale avatar publishes the new fact"),DiskBob.Evidence.Contains(TEXT("ForestFire1")));
            if(!TestTrue(TEXT("Fresh actual replacement pawn"),ReplaceWorldFactPawn(B)))return false;
            // Do not call BindVerifiedPlayer: the production NotifyPawnChanged
            // path must retain the delta observed while this session had no Pawn.
        }
        if(!TestTrue(TEXT("New trusted world event advances idle peer"),PumpRuntimeUntil(*Runtime,[&]
            {return Ready()&&B.Client->GetProfile()->Evidence.Contains(TEXT("ForestFire1"));})))return false;
        if(!TestTrue(TEXT("Server-event peer evidence is durable"),ReadProfile(TEXT("Bob"),DiskBob)))return false;
        TestTrue(TEXT("Both completion routes settle through SQLite"),DiskBob.Evidence.Contains(TEXT("ForestFire1")));
        TestEqual(TEXT("Another fact cannot award water twice"),DiskBob.Gold,RewardedBob.Gold);
        if(HoldOldSettle)
        {
            bool ExitedDuringPublication=false,NestedInstallAccepted=false;
            Runtime->SetWorldPublisher([&](const auto& World)
            {
                if(!World.WorldFactSources.Contains(TEXT("ForestFire2")))return;
                ExitedDuringPublication=true;Runtime->UninstallBackend();FString Reason;
                NestedInstallAccepted=Runtime->InstallBackend(Store,World,Resolve,Publish,Reason);
            });
            Fire.FactId=TEXT("ForestFire2");Fire.SourceId=TEXT("ForestFire2");
            if(!TestTrue(TEXT("Final old-generation fact accepted"),Runtime->ObserveServerFact(Fire,Why))||
                !TestTrue(TEXT("Exit occurs inside actual world completion callback"),PumpRuntimeUntil(*Runtime,[&]{return ExitedDuringPublication;})))return false;
            TestFalse(TEXT("Old callback cannot install a new backend while ticking"),NestedInstallAccepted);
            TestFalse(TEXT("Exited backend immediately rejects new inputs"),Runtime->IsInstalled());
            if(!TestTrue(TEXT("Old generation fully drains and releases bindings"),Runtime->DrainBackend()))return false;
            TestEqual(TEXT("Old pending peer intent cannot retain a connection"),Runtime->Inspect().Connections,0);
            // Reopen the same durable world: historical facts form the next
            // generation's baseline, and ordinary login recovers Bob's missed delta.
            Open=AetherSQLite::Open(O);if(!TestTrue(TEXT("Reopen persisted world after drain"),Open.Store.IsValid()))return false;
            Store=MakeShared<FWorldFactPublicationStore,ESPMode::ThreadSafe>(Open.Store.ToSharedRef());
            if(!TestTrue(TEXT("Read new audited activation value"),ReadWorld(DiskWorld)))return false;
            if(!TestTrue(TEXT("New backend generation gets fresh avatar resource lives"),ReplaceWorldFactPawn(A)&&ReplaceWorldFactPawn(B)))return false;
            if(!TestTrue(TEXT("Install next generation with its own copied baseline"),Runtime->InstallBackend(Store,DiskWorld,Resolve,Publish,Why))||
                !TestTrue(TEXT("Rebind both identities through normal login"),Runtime->BindVerifiedPlayer(A.Controller,TEXT("Alice"))&&Runtime->BindVerifiedPlayer(B.Controller,TEXT("Bob"))))return false;
            if(!TestTrue(TEXT("New generation independently recovers missing persisted fact"),PumpRuntimeUntil(*Runtime,[&]
                {return Ready()&&B.Client->GetProfile()->Evidence.Contains(TEXT("ForestFire2"));})))return false;
            TestEqual(TEXT("New Bob gets only login read and committed refresh"),Store->SnapshotReads.FindRef(TEXT("Bob")),2);
            TestEqual(TEXT("Already-settled Alice gets only her own login read"),Store->SnapshotReads.FindRef(TEXT("Alice")),1);
            if(!TestTrue(TEXT("Read recovered Bob after generation change"),ReadProfile(TEXT("Bob"),DiskBob)))return false;
            TestTrue(TEXT("Generation change cannot duplicate water reward"),DiskBob.Gold==RewardedBob.Gold&&ItemCount(DiskBob,TEXT("TideStaff"))==1);
        }

    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherWorldFactBaselineLifecycleTest,"Aether.V10.Network.AuditedWorldFactBaselineAndInitialRealm",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherWorldFactBaselineLifecycleTest::RunTest(const FString&)
{
    const auto& D=FAetherV10Definitions::Get();FString Why;
    if(!TestTrue(TEXT("Definitions ready"),D.bValid))return false;
    FAetherSqliteOptions O;O.DatabasePath=FPaths::ProjectSavedDir()/TEXT("Automation/WorldFactBaseline")/FGuid::NewGuid().ToString(EGuidFormats::Digits)/TEXT("state.sqlite");
    auto Open=AetherSQLite::Open(O);if(!TestTrue(TEXT("Isolated native world store"),Open.Store.IsValid()))return false;
    FAetherWorldStateV10 Before;FAetherStoredAggregate Row;Row.Key={EAetherAggregateKind::World,TEXT("Main")};
    if(!TestTrue(TEXT("New-world audit may precede persisted realm identity"),AetherWorldCodec::Encode(Before,D.Items,D.Rules,{},Row.Payload,Why))||
        !TestTrue(TEXT("Initialize empty-realm world"),Open.Store->InitializeWorld(Row).Get().Code==EAetherStoreCode::Committed))return false;
    auto* GI=NewObject<UGameInstance>(GEngine);GI->AddToRoot();GI->InitializeStandalone(NAME_None,nullptr);
    auto* W=GI->GetWorld();auto* Runtime=GI->GetSubsystem<UAetherCommandRuntime>();
    ON_SCOPE_EXIT
    {
        if(Runtime)Runtime->DrainBackend();GI->Shutdown();
        if(W){GEngine->DestroyWorldContext(W);W->DestroyWorld(false);}GI->RemoveFromRoot();if(Open.Store)Open.Store->Close();
    };
    if(!TestNotNull(TEXT("Production runtime"),Runtime))return false;
    const FAetherResolveConnectedContext Resolve=[](auto&,const auto&,const auto&,auto&){return false;};
    const FAetherPublishConnectedState Publish=[](auto&,const auto&,const auto*,const auto*){return true;};
    if(!TestTrue(TEXT("Activation can install before first physical checkpoint assigns realm"),Runtime->InstallBackend(Open.Store.ToSharedRef(),Before,Resolve,Publish,Why)))return false;
    TestFalse(TEXT("Empty identity never authorizes a connection domain"),Runtime->SetBackendDomain({}));
    auto Checkpoint=Before;Checkpoint.RealmId=FGuid::NewGuid();++Checkpoint.Revision;
    FAetherAggregateWrite Write;Write.ExpectedRevision=Before.Revision;Write.Value.Key=Row.Key;Write.Value.Revision=Checkpoint.Revision;
    if(!TestTrue(TEXT("Encode confirmed initial checkpoint"),AetherWorldCodec::Encode(Checkpoint,D.Items,D.Rules,{},Write.Value.Payload,Why)))return false;
    const auto Saved=Open.Store->CompareExchangeWorld(MoveTemp(Write)).Get();
    if(!TestTrue(TEXT("First checkpoint really commits realm"),Saved.Code==EAetherStoreCode::Committed))return false;
    TestTrue(TEXT("First persisted realm completes existing startup handshake"),Runtime->SetBackendDomain(Checkpoint.RealmId));
    TestTrue(TEXT("Repeated startup tick accepts the same realm"),Runtime->SetBackendDomain(Checkpoint.RealmId));
    TestFalse(TEXT("Different realm requires backend replacement even with no players"),Runtime->SetBackendDomain(FGuid::NewGuid()));
    TestTrue(TEXT("Backend exit drops the copied baseline"),Runtime->DrainBackend());
    Open=AetherSQLite::Open(O);if(!TestTrue(TEXT("Reopen exact persisted identity"),Open.Store.IsValid()))return false;
    const auto Reloaded=Open.Store->Read({EAetherAggregateKind::World,TEXT("Main")}).Get();FAetherWorldStateV10 AuditedReload;
    if(!TestTrue(TEXT("Read exact persisted realm after reopen"),Reloaded.Code==EAetherStoreCode::Found&&Reloaded.Value.IsSet()&&
        AetherWorldCodec::Decode(Reloaded.Value->Payload,D.Items,D.Rules,{},AuditedReload,Why)&&AuditedReload.Revision==Reloaded.Value->Revision&&
        AuditedReload.RealmId==Checkpoint.RealmId))return false;
    if(!TestTrue(TEXT("Reactivation accepts already-persisted realm baseline"),Runtime->InstallBackend(Open.Store.ToSharedRef(),AuditedReload,Resolve,Publish,Why)))return false;
    TestFalse(TEXT("Reactivation cannot replace the audited realm"),Runtime->SetBackendDomain(FGuid::NewGuid()));
    TestTrue(TEXT("Reactivation still accepts its own realm"),Runtime->SetBackendDomain(Checkpoint.RealmId));
    return true;
}
#endif
