#include "Misc/AutomationTest.h"
#include "Commands/AetherProfileCoordinator.h"
#include "Persistence/AetherSqliteStore.h"
#include "Profile/AetherProfileCodec.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#if WITH_DEV_AUTOMATION_TESTS
namespace
{
FAetherCommandResult Wait(FAetherProfileCoordinator& Service,const FAetherResolveProfileContext& Context,FAetherProfileCompletion& Completion)
{
    const double Deadline=FPlatformTime::Seconds()+5;
    while(FPlatformTime::Seconds()<Deadline)
    {
        auto Results=Service.Poll(Context);
        if(!Results.IsEmpty()){Completion=MoveTemp(Results[0]);return Completion.Result;}
        FPlatformProcess::Sleep(.001f);
    }
    Completion={};Completion.Result.Code=EAetherCommandCode::Busy;return Completion.Result;
}
FAetherPlayerCommand Command(EAetherCommandType Type,int64 Revision)
{
    FAetherPlayerCommand C;C.Type=Type;C.ExpectedProfileRevision=Revision;C.CommandId=AetherTransactions::NewCommandId(Revision);return C;
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherCoordinatorTest,"Aether.V10.Commands.ProfileCommitReplayAndEpoch",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherCoordinatorTest::RunTest(const FString&)
{
    FString Json,Reason;FFileHelper::LoadFileToString(Json,*(FPaths::ProjectContentDir()/TEXT("AetherCore/Definitions/V10/Items.json")));
    const auto Items=FAetherV10ItemDefinitions::Parse(Json,Reason);const auto& Rules=FAetherRules::Get();const auto& Skills=FAetherSkillDefinitionsV10::Get();
    FAetherProfileStateV10 Profile;Profile.CharacterId=TEXT("Alice");Profile.Claims={TEXT("Q_Main_02")};
    Profile.Skills.GrantStory(TEXT("Fire.Ignite"),TEXT("Story.Training"),Skills);Profile.Skills.AwardPoints(TEXT("Test.Points"),3,Skills);
    FAetherV10ItemInstance Potion;Potion.InstanceId=FGuid(1,2,3,4);Potion.DefinitionId=TEXT("Potion");Potion.Quantity=10;Potion.SlotIndex=0;Profile.Inventory.Items.Add(Potion);
    FAetherSqliteOptions O;O.DatabasePath=FPaths::ProjectSavedDir()/TEXT("Automation/V10Coordinator")/FGuid::NewGuid().ToString(EGuidFormats::Digits)/TEXT("state.sqlite");
    O.Fault=MakeShared<TAtomic<EAetherStoreFault>,ESPMode::ThreadSafe>(EAetherStoreFault::None);
    auto DB=AetherSQLite::Open(O);if(!TestTrue(TEXT("Open isolated real store"),DB.Store.IsValid()))return false;
    FAetherTransaction Seed;Seed.ActorId=Profile.CharacterId;Seed.ExpectedProfileRevision=-1;Seed.CommandId=AetherTransactions::NewCommandId(-1);Seed.Request={1};
    FAetherAggregateWrite Row;Row.Value.Key={EAetherAggregateKind::Profile,Profile.CharacterId};
    if(!TestTrue(*Reason,AetherProfileCodec::Encode(Profile,Items,Skills,Rules,Row.Value.Payload,Reason))){DB.Store->Close();return false;}
    Seed.Writes.Add(Row);TestTrue(TEXT("Seed committed profile"),DB.Store->Commit(Seed).Get().Code==EAetherStoreCode::Committed);
    FAetherProfileCoordinator Service(DB.Store.ToSharedRef(),Items,Skills,Rules);
    auto Session=Service.BeginSession(Profile.CharacterId);FAetherCommandResult Reject;FAetherProfileCompletion Done;
    int32 Resolved=0;
    const FAetherResolveProfileContext Context=[&](const auto&,const auto&,FAetherProfileCommandContext& C)
    {++Resolved;C.bCanManageInventory=true;C.Skill.bAtResetService=true;return true;};
    auto Split=Command(EAetherCommandType::SplitStack,0);Split.ItemInstanceId=Potion.InstanceId;Split.Quantity=3;Split.DestinationIndex=4;
    TestTrue(TEXT("Valid session accepted"),Service.Submit(Session,Split,Reject));
    TestFalse(TEXT("One command per character in flight"),Service.Submit(Session,Split,Reject));TestTrue(TEXT("Backpressure is explicit"),Reject.Code==EAetherCommandCode::Busy);
    const auto Before=DB.Store->Read(Row.Value.Key).Get();TestEqual(TEXT("Submission does not optimistically mutate profile"),Before.Value->Revision,int64(0));
    TestTrue(TEXT("Split commits via coordinator"),Wait(Service,Context,Done).Code==EAetherCommandCode::Applied);
    if(!TestTrue(TEXT("Only committed current-session snapshot can publish"),Done.bMayPublish&&Done.Snapshot.IsSet())){DB.Store->Close();return false;}
    TestTrue(TEXT("Split quantity and new identity persist"),Done.Snapshot->Inventory.At(0)->Quantity==7&&Done.Snapshot->Inventory.At(4)->Quantity==3&&Done.Result.AffectedIds.Num()==2);
    const auto NewId=Done.Snapshot->Inventory.At(4)->InstanceId;
    TestEqual(TEXT("Exactly one aggregate version advance"),Done.Snapshot->Revision,int64(1));
    const int32 BeforeReplay=Resolved;
    TestTrue(TEXT("Same bytes can retry after state changed"),Service.Submit(Session,Split,Reject));
    TestTrue(TEXT("Receipt replay precedes stale revision and source quantity checks"),Wait(Service,Context,Done).Code==EAetherCommandCode::Replayed);
    TestEqual(TEXT("Replay never invokes domain evaluator"),Resolved,BeforeReplay);
    TestTrue(TEXT("No second split identity"),Done.Snapshot.IsSet()&&Done.Snapshot->Inventory.Items.Num()==2&&Done.Snapshot->Inventory.At(4)->InstanceId==NewId);
    auto Changed=Split;Changed.Quantity=2;Service.Submit(Session,Changed,Reject);
    TestTrue(TEXT("Same ID different wire bytes conflicts before rules"),Wait(Service,Context,Done).Code==EAetherCommandCode::Conflict);
    auto Stale=Command(EAetherCommandType::SetItemFavorite,0);Stale.ItemInstanceId=Potion.InstanceId;Stale.Enabled=true;Service.Submit(Session,Stale,Reject);
    TestTrue(TEXT("New old-version request cannot execute"),Wait(Service,Context,Done).Code==EAetherCommandCode::StaleRevision&&Done.Snapshot.IsSet());
    auto Lock=Command(EAetherCommandType::SetItemLock,1);Lock.ItemInstanceId=Potion.InstanceId;Lock.Enabled=true;
    O.Fault->Store(EAetherStoreFault::AfterFirstWrite);Service.Submit(Session,Lock,Reject);
    TestTrue(TEXT("Storage failure cannot publish candidate"),Wait(Service,Context,Done).Code==EAetherCommandCode::StorageUnavailable&&!Done.Snapshot.IsSet());
    auto Still=DB.Store->Read(Row.Value.Key).Get();TestEqual(TEXT("Failed lock did not advance storage"),Still.Value->Revision,int64(1));
    Service.Submit(Session,Lock,Reject);TestTrue(TEXT("Retry after rollback succeeds once"),Wait(Service,Context,Done).Code==EAetherCommandCode::Applied&&Done.Snapshot->Inventory.Find(Potion.InstanceId)->bLocked);
    auto Upgrade=Command(EAetherCommandType::UpgradeSkill,2);Upgrade.SkillId=TEXT("Fire.Ignite");
    O.Fault->Store(EAetherStoreFault::AfterCommitBeforeReply);Service.Submit(Session,Upgrade,Reject);
    TestTrue(TEXT("Lost committed response still does not publish speculative state"),Wait(Service,Context,Done).Code==EAetherCommandCode::StorageUnavailable&&!Done.Snapshot.IsSet());
    Service.Submit(Session,Upgrade,Reject);
    TestTrue(TEXT("Lost response retry recovers paid skill result"),Wait(Service,Context,Done).Code==EAetherCommandCode::Replayed);
    TestTrue(TEXT("Points and rank changed in one durable profile"),Done.Snapshot.IsSet()&&Done.Snapshot->Revision==3&&Done.Snapshot->Skills.PermanentRank(TEXT("Fire.Ignite"))==2&&Done.Snapshot->Skills.AvailableSkillPoints==2);
    const auto OldSession=Session;Session=Service.BeginSession(TEXT("Alice"));
    Service.EndSession(OldSession);TestTrue(TEXT("Old logout cannot destroy replacement session"),Service.IsCurrent(Session));
    TestFalse(TEXT("Old connection cannot submit"),Service.Submit(OldSession,Upgrade,Reject));
    auto Move=Command(EAetherCommandType::MoveItem,3);Move.ItemInstanceId=NewId;Move.DestinationIndex=5;
    Service.Submit(Session,Move,Reject);const int32 BeforeMove=Resolved;const double Deadline=FPlatformTime::Seconds()+5;
    while(Resolved==BeforeMove&&FPlatformTime::Seconds()<Deadline){Service.Poll(Context);FPlatformProcess::Sleep(.001f);}
    TestEqual(TEXT("Reached commit boundary before pawn replacement"),Resolved,BeforeMove+1);
    const auto PriorPawn=Session;Session=Service.ReplacePawn(Session);
    Wait(Service,Context,Done);
    TestTrue(TEXT("Committed old-pawn result cannot publish to replacement"),!Done.bMayPublish&&!Done.Snapshot.IsSet()&&Done.Session==PriorPawn);
    Service.Submit(Session,Move,Reject);
    TestTrue(TEXT("New pawn can recover committed result via durable receipt"),Wait(Service,Context,Done).Code==EAetherCommandCode::Replayed&&Done.bMayPublish&&Done.Snapshot.IsSet()&&Done.Snapshot->Revision==4&&Done.Snapshot->Inventory.At(5)->InstanceId==NewId);
    // 在提交前失效则直接撤销，不产生回执或资产变化。
    auto Cancel=Command(EAetherCommandType::SetItemFavorite,4);Cancel.ItemInstanceId=NewId;Cancel.Enabled=true;
    Service.Submit(Session,Cancel,Reject);Service.EndSession(Session);Wait(Service,Context,Done);
    TestTrue(TEXT("Disconnected pre-commit work cannot publish"),!Done.bMayPublish);
    TArray<uint8> Wire;AetherCommands::Encode(Cancel,Wire,Reason);
    TestTrue(TEXT("No receipt for cancelled pre-commit work"),DB.Store->LookupReceipt({TEXT("Alice"),Cancel.CommandId,1,Wire}).Get().Code==EAetherStoreCode::Missing);
    TestEqual(TEXT("No abandoned jobs"),Service.PendingCount(),0);
    // 后端有意把 BLOB 视为不透明值；领域加载器必须发现行版本/内部版本不一致，而不是自动修档。
    const auto Stored=DB.Store->Read(Row.Value.Key).Get();FAetherProfileStateV10 Poison;
    if(!TestTrue(TEXT("Read synthetic state before corruption probe"),AetherProfileCodec::Decode(Stored.Value->Payload,Items,Skills,Rules,Poison,Reason)))
    {DB.Store->Close();return false;}
    Poison.Revision=99;FAetherTransaction Corrupt;Corrupt.ActorId=TEXT("Alice");Corrupt.CommandId=AetherTransactions::NewCommandId(4);Corrupt.ExpectedProfileRevision=4;Corrupt.Request={99};
    FAetherAggregateWrite BadRow;BadRow.ExpectedRevision=4;BadRow.Value.Key=Row.Value.Key;BadRow.Value.Revision=5;
    AetherProfileCodec::Encode(Poison,Items,Skills,Rules,BadRow.Value.Payload,Reason);Corrupt.Writes.Add(BadRow);
    TestTrue(TEXT("Opaque backend stores intentionally mismatched synthetic DTO"),DB.Store->Commit(Corrupt).Get().Code==EAetherStoreCode::Committed);
    Session=Service.BeginSession(TEXT("Alice"));auto BadRead=Command(EAetherCommandType::SetItemFavorite,5);BadRead.ItemInstanceId=NewId;BadRead.Enabled=true;
    const int32 BeforeBadRead=Resolved;Service.Submit(Session,BadRead,Reject);
    TestTrue(TEXT("Row and DTO version mismatch fails closed"),Wait(Service,Context,Done).Code==EAetherCommandCode::StorageUnavailable&&!Done.Snapshot.IsSet());
    TestEqual(TEXT("Corrupt stored profile never reaches domain evaluation"),Resolved,BeforeBadRead);
    DB.Store->Close();return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherProfilePrepareTest,"Aether.V10.Commands.ProfileCandidateAuthorityAndFailure",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherProfilePrepareTest::RunTest(const FString&)
{
    FString Json,Reason;FFileHelper::LoadFileToString(Json,*(FPaths::ProjectContentDir()/TEXT("AetherCore/Definitions/V10/Items.json")));
    const auto Items=FAetherV10ItemDefinitions::Parse(Json,Reason);const auto& Rules=FAetherRules::Get();const auto& Skills=FAetherSkillDefinitionsV10::Get();
    FAetherProfileStateV10 P;P.CharacterId=TEXT("Alice");P.Skills.GrantStory(TEXT("Fire.Ignite"),TEXT("Story.Training"),Skills);P.Skills.AwardPoints(TEXT("Test.Points"),3,Skills);
    auto C=Command(EAetherCommandType::UpgradeSkill,0);C.SkillId=TEXT("Fire.Ignite");
    FAetherProfileCommandContext Context;Context.Skill.CompletedQuests.Add(TEXT("Q_Main_02"));
    FAetherTransaction T;T.ActorId=TEXT("Sentinel");FAetherCommandResult R;
    const auto Prepare=[&](const FString& Actor){return AetherProfileCommands::Prepare(C,Actor,P,Context,Items,Skills,Rules,T,R);};
    TestFalse(TEXT("Cannot modify another server-resolved identity"),Prepare(TEXT("Mallory")));
    TestTrue(TEXT("Unauthorized result"),R.Code==EAetherCommandCode::Unauthorized);
    TestFalse(TEXT("Caller cannot supply missing completed quest"),Prepare(TEXT("Alice")));
    TestEqual(TEXT("Failure preserves old transaction output"),T.ActorId,FString(TEXT("Sentinel")));
    P.Claims={TEXT("Q_Main_02")};Context.Skill.bInCombat=true;TestFalse(TEXT("Current combat state blocks skill upgrade"),Prepare(TEXT("Alice")));
    Context.Skill.bInCombat=false;TestTrue(TEXT("Authoritative profile and context produce candidate"),Prepare(TEXT("Alice")));
    TestEqual(TEXT("Prepare never publishes to source profile"),P.Skills.PermanentRank(TEXT("Fire.Ignite")),1);
    FAetherProfileStateV10 Next;TestTrue(TEXT("Candidate is complete profile DTO"),AetherProfileCodec::Decode(T.Writes[0].Value.Payload,Items,Skills,Rules,Next,Reason)&&Next.Revision==1&&Next.Skills.AvailableSkillPoints==2);
    C.Type=EAetherCommandType::LearnSkill;TestFalse(TEXT("Learn does not silently upgrade existing skill"),Prepare(TEXT("Alice")));
    C=Command(EAetherCommandType::ResetSkills,0);TestFalse(TEXT("Reset requires current service authorization"),Prepare(TEXT("Alice")));
    C=Command(EAetherCommandType::DropItem,0);C.ItemInstanceId=FGuid(9,8,7,6);C.Quantity=1;C.ExpectedWorldRevision=0;
    TestFalse(TEXT("Unimplemented cross-domain commands never succeed as no-op"),Prepare(TEXT("Alice")));
    TestTrue(TEXT("Explicit unsupported action result"),R.Code==EAetherCommandCode::UnsupportedAction);
    FAetherV10ItemInstance I;I.DefinitionId=TEXT("Potion");I.InstanceId=FGuid(1,2,3,4);I.SlotIndex=0;I.Quantity=3;P.Inventory.Items.Add(I);
    I.InstanceId=FGuid(5,6,7,8);I.SlotIndex=1;I.Quantity=2;P.Inventory.Items.Add(I);
    C=Command(EAetherCommandType::SortInventory,0);C.Enabled=true;Context.bCanManageInventory=true;
    TestTrue(TEXT("Sort candidate records consumed source identities"),Prepare(TEXT("Alice")));
    FAetherCommandResult Reply;
    TestTrue(TEXT("Sort merge relations survive durable result codec"),AetherCommands::DecodeResult(T.Result,Reply,Reason)&&Reply.Transfers.Num()==1&&Reply.AffectedIds.Contains(Reply.Transfers[0].From)&&Reply.AffectedIds.Contains(Reply.Transfers[0].To));
    return true;
}
#endif
