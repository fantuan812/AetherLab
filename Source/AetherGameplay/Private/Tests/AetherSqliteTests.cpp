#include "Misc/AutomationTest.h"
#include "Persistence/AetherSqliteStore.h"
#include "Inventory/AetherInventoryCodec.h"
#include "Profile/AetherProfileCodec.h"
#include "../Persistence/AetherSqliteInternal.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
constexpr auto TestFlags=EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter;
FAetherSqliteOptions TestOptions()
{
    FAetherSqliteOptions O;
    O.DatabasePath=FPaths::ProjectSavedDir()/TEXT("Automation/V10Store")/FGuid::NewGuid().ToString(EGuidFormats::Digits)/TEXT("state.sqlite");
    O.Fault=MakeShared<TAtomic<EAetherStoreFault>,ESPMode::ThreadSafe>(EAetherStoreFault::None);
    return O;
}
FAetherTransaction Request(int64 Revision, uint8 Value, bool WithWorld=false)
{
    FAetherTransaction T;
    T.ActorId=TEXT("SyntheticAlice");
    T.ExpectedProfileRevision=Revision;
    T.CommandId=AetherTransactions::NewCommandId(Revision);
    T.Request={Value,uint8(Revision+1)}; T.Result={0xAC,Value};
    FAetherAggregateWrite Profile;
    Profile.ExpectedRevision=Revision;
    Profile.Value.Key={EAetherAggregateKind::Profile,T.ActorId};
    Profile.Value.Revision=Revision+1; Profile.Value.Payload={Value};
    T.Writes.Add(Profile);
    if (WithWorld)
    {
        auto World=Profile; World.Value.Key={EAetherAggregateKind::World,TEXT("SyntheticWorld")};
        T.Writes.Add(World);
    }
    return T;
}
int64 Revision(IAetherTransactionalStore& Store, EAetherAggregateKind Kind=EAetherAggregateKind::Profile)
{
    auto R=Store.Read({Kind,Kind==EAetherAggregateKind::Profile?TEXT("SyntheticAlice"):TEXT("SyntheticWorld")}).Get();
    return R.Code==EAetherStoreCode::Found && R.Value.IsSet()?R.Value->Revision:-999;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherStoreAtomicTest,"Aether.V10.Store.AtomicRecoveryAndBackup",TestFlags)
bool FAetherStoreAtomicTest::RunTest(const FString&)
{
    TestEqual(TEXT("Pinned SQLite is actually linked"),AetherSQLite::RuntimeVersion(),FString(TEXT("3.53.4")));
    auto Options=TestOptions();
    auto Opened=AetherSQLite::Open(Options);
    if (!TestTrue(TEXT("Open durable writer"),Opened.Code==EAetherStoreCode::Ready && Opened.Store.IsValid()))
    { AddError(Opened.Detail); return false; }
    auto Store=Opened.Store;
    TestTrue(TEXT("Missing is explicit"),Store->Read({EAetherAggregateKind::Profile,TEXT("Nobody")}).Get().Code==EAetherStoreCode::Missing);
    auto First=Request(-1,1,true);
    TestTrue(TEXT("Create profile and world together"),Store->Commit(First).Get().Code==EAetherStoreCode::Committed);
    auto Second=Request(0,2,true);
    FAetherEffectDelivery Effect;
    Effect.ActorId=Second.ActorId; Effect.Id=AetherTransactions::NewCommandId(0); Effect.Payload={90,100};
    Second.Effects.Add(Effect);
    Options.Fault->Store(EAetherStoreFault::AfterFirstWrite);
    auto Failed=Store->Commit(Second).Get();
    TestTrue(TEXT("Injected mid-write fails"),Failed.Code==EAetherStoreCode::Unavailable);
    TestEqual(TEXT("Failure reports committed revision"),Failed.FinalProfileRevision,int64(0));
    TestEqual(TEXT("Profile rolled back"),Revision(*Store),int64(0));
    TestEqual(TEXT("World rolled back"),Revision(*Store,EAetherAggregateKind::World),int64(0));
    TestEqual(TEXT("No effect before commit"),Store->PendingEffects(Second.ActorId).Get().Values.Num(),0);
    TestTrue(TEXT("Same ID can retry uncommitted transaction"),Store->Commit(Second).Get().Code==EAetherStoreCode::Committed);
    Store->Close(); Store.Reset(); Opened.Store.Reset();

    Opened=AetherSQLite::Open(Options); Store=Opened.Store;
    if (!TestTrue(TEXT("Reopen exact committed database"),Store.IsValid())) { AddError(Opened.Detail); return false; }
    TestTrue(TEXT("Durable receipt replays after reopen"),Store->Commit(Second).Get().Code==EAetherStoreCode::Replayed);
    TestEqual(TEXT("Reopen does not replay writes"),Revision(*Store),int64(1));
    auto Effects=Store->PendingEffects(Second.ActorId).Get();
    TestTrue(TEXT("Effect survives restart"),Effects.Code==EAetherStoreCode::Found && Effects.Values.Num()==1 && Effects.Values[0].Id==Effect.Id && Effects.Values[0].Payload==Effect.Payload);
    Store->AcknowledgeEffect(TEXT("OtherActor"),Effect.Id).Get();
    TestEqual(TEXT("Other actor cannot remove delivery"),Store->PendingEffects(Second.ActorId).Get().Values.Num(),1);
    TestTrue(TEXT("Acknowledge committed effect"),Store->AcknowledgeEffect(Second.ActorId,Effect.Id).Get());
    Store->Commit(Second).Get();
    TestEqual(TEXT("Retry cannot re-create acknowledged effect"),Store->PendingEffects(Second.ActorId).Get().Values.Num(),0);
    auto Third=Request(1,3,true);
    Third.Effects.Add({AetherTransactions::NewCommandId(1),Third.ActorId,1,{70,100}});
    Options.Fault->Store(EAetherStoreFault::AfterCommitBeforeReply);
    TestTrue(TEXT("Response may be lost after commit"),Store->Commit(Third).Get().Code==EAetherStoreCode::Unavailable);
    TestEqual(TEXT("Lost response still committed both domains"),Revision(*Store,EAetherAggregateKind::World),int64(2));
    TestTrue(TEXT("Lost response retry is replay"),Store->Commit(Third).Get().Code==EAetherStoreCode::Replayed);
    TestEqual(TEXT("One durable effect only"),Store->PendingEffects(Third.ActorId).Get().Values.Num(),1);
    auto Fourth=Request(2,4,true);
    Options.Fault->Store(EAetherStoreFault::BeforeCommit);
    TestTrue(TEXT("Precommit failure is not success"),Store->Commit(Fourth).Get().Code==EAetherStoreCode::Unavailable);
    TestEqual(TEXT("Precommit rollback retains old profile"),Revision(*Store),int64(2));
    TestEqual(TEXT("Precommit rollback retains old world"),Revision(*Store,EAetherAggregateKind::World),int64(2));
    auto Changed=Third; Changed.Request.Add(7);
    TestTrue(TEXT("Same ID changed request rejected"),Store->Commit(Changed).Get().Code==EAetherStoreCode::Conflict);

    const FString Backup=FPaths::GetPath(Options.DatabasePath)/TEXT("consistent-backup.sqlite");
    TestTrue(TEXT("Online WAL backup succeeds"),Store->Backup(Backup).Get());
    TestFalse(TEXT("Existing backup not overwritten"),Store->Backup(Backup).Get());
    auto BackupOptions=Options; BackupOptions.DatabasePath=Backup;
    auto BackupStore=AetherSQLite::Open(BackupOptions);
    if (TestTrue(TEXT("Backup opens independently"),BackupStore.Store.IsValid()))
    {
        TestEqual(TEXT("Backup profile revision"),Revision(*BackupStore.Store),int64(2));
        TestEqual(TEXT("Backup world revision"),Revision(*BackupStore.Store,EAetherAggregateKind::World),int64(2));
        TestEqual(TEXT("Backup retains undelivered effect"),BackupStore.Store->PendingEffects(Third.ActorId).Get().Values.Num(),1);
        BackupStore.Store->Close();
    }
    Store->Close();
    TestTrue(TEXT("Closed read is not missing record"),Store->Read(First.Writes[0].Value.Key).Get().Code==EAetherStoreCode::Unavailable);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherStoreExpiryTest,"Aether.V10.Store.BoundedReceiptsAndConcurrentWriters",TestFlags)
bool FAetherStoreExpiryTest::RunTest(const FString&)
{
    auto Options=TestOptions(); auto Opened=AetherSQLite::Open(Options);
    if (!TestTrue(TEXT("Open writer"),Opened.Store.IsValid())) { AddError(Opened.Detail); return false; }
    auto Store=Opened.Store; const auto First=Request(-1,1);
    TestTrue(TEXT("First commit"),Store->Commit(First).Get().Code==EAetherStoreCode::Committed);
    for (int32 I=1;I<=65;++I)
        if (!TestTrue(TEXT("Advance bounded receipt window"),Store->Commit(Request(I-1,uint8(I))).Get().Code==EAetherStoreCode::Committed)) return false;
    TestTrue(TEXT("Evicted old request cannot execute again"),Store->Commit(First).Get().Code==EAetherStoreCode::Expired);
    auto Reused=Request(65,99); Reused.CommandId=First.CommandId;
    TestTrue(TEXT("Old ID cannot be rebound to new version"),Store->Commit(Reused).Get().Code==EAetherStoreCode::Invalid);
    auto Other=AetherSQLite::Open(Options);
    if (!TestTrue(TEXT("Second OS connection opens"),Other.Store.IsValid())) { AddError(Other.Detail); return false; }
    auto A=Store->Commit(Request(65,10)); auto B=Other.Store->Commit(Request(65,11));
    const auto AR=A.Get().Code, BR=B.Get().Code;
    TestTrue(TEXT("Exactly one competing writer commits"),(AR==EAetherStoreCode::Committed && BR==EAetherStoreCode::Expired)||(BR==EAetherStoreCode::Committed && AR==EAetherStoreCode::Expired));
    TestEqual(TEXT("Conflict advances once"),Revision(*Store),int64(66));
    Other.Store->Close(); Store->Close();

    // 直接检查物理回执数量，不能仅因旧请求失败便声称存储有界。
    sqlite3* DB=nullptr; FTCHARToUTF8 Path(*Options.DatabasePath);
    TestEqual(TEXT("Inspect test database"),sqlite3_open(Path.Get(),&DB),SQLITE_OK);
    if (DB)
    {
        { AetherSQLite::Private::FStatement Count(DB,"SELECT count(*) FROM receipts"); TestTrue(TEXT("Receipt index is capped at 64"),Count.Step()==SQLITE_ROW && Count.ColumnInt(0)==64); }
        TestTrue(TEXT("Install unknown future schema fixture"),AetherSQLite::Private::Exec(DB,"PRAGMA user_version=999"));
        sqlite3_close(DB);
    }
    TArray<uint8> Before,After; FFileHelper::LoadFileToArray(Before,*Options.DatabasePath);
    auto FutureSchema=AetherSQLite::Open(Options);
    TestTrue(TEXT("Unknown major schema explicitly rejected"),FutureSchema.Code==EAetherStoreCode::UnsupportedSchema && !FutureSchema.Store);
    FFileHelper::LoadFileToArray(After,*Options.DatabasePath);
    TestTrue(TEXT("Unknown database preserved byte-for-byte"),Before==After);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherStoreContractTest,"Aether.V10.Store.ContractAndCloseDrain",TestFlags)
bool FAetherStoreContractTest::RunTest(const FString&)
{
    FString Reason;
    auto T=Request(-1,1);
    TestTrue(TEXT("Valid shape"),AetherTransactions::Validate(T,Reason));
    auto Invalid=T; Invalid.ProtocolVersion=999;
    TestFalse(TEXT("Unknown protocol rejected"),AetherTransactions::Validate(Invalid,Reason));
    Invalid=T; const auto DuplicateWrite=Invalid.Writes[0]; Invalid.Writes.Add(DuplicateWrite);
    TestFalse(TEXT("Duplicate aggregate write rejected"),AetherTransactions::Validate(Invalid,Reason));
    Invalid=T; Invalid.Writes[0].Value.Revision=42;
    TestFalse(TEXT("Skipped revision rejected"),AetherTransactions::Validate(Invalid,Reason));
    Invalid=T; Invalid.Writes[0].Value.Key.Id=TEXT("OtherActor");
    TestFalse(TEXT("Missing actor version rejected"),AetherTransactions::Validate(Invalid,Reason));
    Invalid=T; Invalid.Writes[0].Value.Payload.SetNum(AetherTransactions::MaxPayloadBytes+1);
    TestFalse(TEXT("Unbounded payload rejected"),AetherTransactions::Validate(Invalid,Reason));
    auto O=TestOptions(); auto Opened=AetherSQLite::Open(O);
    if (!TestTrue(TEXT("Open close-drain fixture"),Opened.Store.IsValid())) { AddError(Opened.Detail); return false; }
    TArray<TFuture<FAetherStoreResult>> Pending;
    for (int32 I=0;I<10;++I) Pending.Add(Opened.Store->Commit(Request(I-1,uint8(I))));
    Opened.Store->Close();
    for (auto& Future:Pending) TestTrue(TEXT("Close drains accepted work"),Future.Get().Code==EAetherStoreCode::Committed);
    auto Reopened=AetherSQLite::Open(O);
    if (TestTrue(TEXT("Drained database reopens"),Reopened.Store.IsValid()))
    {
        TestEqual(TEXT("All ten writes persisted"),Revision(*Reopened.Store),int64(9));
        Reopened.Store->Close();
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherInventoryStoreTest,"Aether.V10.Store.InventoryDTORecovery",TestFlags)
bool FAetherInventoryStoreTest::RunTest(const FString&)
{
    FString Text,Reason;FFileHelper::LoadFileToString(Text,*(FPaths::ProjectContentDir()/TEXT("AetherCore/Definitions/V10/Items.json")));
    const auto D=FAetherV10ItemDefinitions::Parse(Text,Reason);if(!TestTrue(*Reason,D.Validate(Reason)))return false;
    FAetherInventoryStateV10 S;FAetherV10ItemInstance I;
    I.InstanceId=FGuid(1,2,3,4);I.DefinitionId=TEXT("LeatherVest");I.SlotIndex=17;I.Durability=9;
    I.BoundToCharacter=TEXT("SyntheticAlice");I.bLocked=true;I.Affixes.Add(TEXT("Roll"),7);S.Items.Add(I);
    S.Equip(I.InstanceId,TEXT("Chest"),TEXT("SyntheticAlice"),D);
    auto Options=TestOptions();auto Opened=AetherSQLite::Open(Options);
    if(!TestTrue(TEXT("Open inventory store"),Opened.Code==EAetherStoreCode::Ready))return false;
    auto T=Request(-1,1);
    if(!TestTrue(TEXT("Encode explicit inventory payload"),AetherInventoryCodec::Encode(S,D,T.Writes[0].Value.Payload,Reason)))return false;
    TestTrue(TEXT("Persist inventory candidate"),Opened.Store->Commit(T).Get().Code==EAetherStoreCode::Committed);
    Opened.Store->Close();Opened.Store.Reset();
    Opened=AetherSQLite::Open(Options);
    if(!TestTrue(TEXT("Reopen inventory store"),Opened.Code==EAetherStoreCode::Ready))return false;
    auto Row=Opened.Store->Read({EAetherAggregateKind::Profile,TEXT("SyntheticAlice")}).Get();
    if(!TestTrue(TEXT("Recover committed inventory row"),Row.Code==EAetherStoreCode::Found&&Row.Value.IsSet()))return false;
    FAetherInventoryStateV10 Loaded;
    if(!TestTrue(*Reason,AetherInventoryCodec::Decode(Row.Value->Payload,D,Loaded,Reason)))return false;
    TestTrue(TEXT("Database roundtrip preserves all instance metadata"),Loaded.Find(I.InstanceId)&&Loaded.Find(I.InstanceId)->SameStackKey(I));
    TestTrue(TEXT("Sparse grid and equipment reference survive close/reopen"),Loaded.At(17)&&Loaded.Equipment.FindRef(TEXT("Chest"))==I.InstanceId);
    auto Candidate=Loaded;Candidate.Repair(I.InstanceId,D);
    auto Next=Request(0,2);AetherInventoryCodec::Encode(Candidate,D,Next.Writes[0].Value.Payload,Reason);
    Options.Fault->Store(EAetherStoreFault::AfterFirstWrite);
    TestTrue(TEXT("Injected failure rejects replacement inventory"),Opened.Store->Commit(Next).Get().Code!=EAetherStoreCode::Committed);
    Options.Fault->Store(EAetherStoreFault::None);
    Row=Opened.Store->Read({EAetherAggregateKind::Profile,TEXT("SyntheticAlice")}).Get();
    TestTrue(TEXT("Old durable state retained after write failure"),Row.Value.IsSet()&&AetherInventoryCodec::Decode(Row.Value->Payload,D,Loaded,Reason)&&Loaded.Find(I.InstanceId)->Durability==9);
    Opened.Store->Close();return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherProfileStoreTest,"Aether.V10.Store.FullProfileAtomicGrowthAndRecovery",TestFlags)
bool FAetherProfileStoreTest::RunTest(const FString&)
{
    FString Json,Reason;FFileHelper::LoadFileToString(Json,*(FPaths::ProjectContentDir()/TEXT("AetherCore/Definitions/V10/Items.json")));
    const auto Items=FAetherV10ItemDefinitions::Parse(Json,Reason);
    const auto& Skills=FAetherSkillDefinitionsV10::Get();const auto& Rules=FAetherRules::Get();
    FAetherProfileStateV10 S;S.CharacterId=TEXT("SyntheticAlice");S.Gold=100;
    FAetherV10ItemInstance Item;Item.InstanceId=FGuid(4,5,6,7);Item.DefinitionId=TEXT("Potion");Item.Quantity=5;Item.SlotIndex=6;S.Inventory.Items.Add(Item);
    S.Skills.GrantStory(TEXT("Fire.Ignite"),TEXT("Story.Training"),Skills);S.Skills.AwardPoints(TEXT("Quest.Test"),3,Skills);
    FAetherPendingRewardV10 Reward;Reward.RewardId=FGuid(7,6,5,4);Reward.SourceId=TEXT("Quest.Pending");Reward.Gold=10;S.PendingRewards.Add(Reward);
    auto Options=TestOptions();auto Opened=AetherSQLite::Open(Options);
    if(!TestTrue(TEXT("Open full profile fixture"),Opened.Store.IsValid()))return false;
    auto Initial=Request(-1,1);
    if(!TestTrue(TEXT("Encode full profile"),AetherProfileCodec::Encode(S,Items,Skills,Rules,Initial.Writes[0].Value.Payload,Reason)))return false;
    TestTrue(TEXT("Create complete profile"),Opened.Store->Commit(Initial).Get().Code==EAetherStoreCode::Committed);
    auto Candidate=S;Candidate.Revision=1;Candidate.Gold=90;Candidate.Inventory.Items[0].Quantity=4;
    FAetherSkillRuleContext C;C.CompletedQuests.Add(TEXT("Q_Main_02"));
    auto Transaction=Request(0,2);
    TestTrue(TEXT("Prepare real rank/cost transition"),Candidate.Skills.LearnNext(TEXT("Fire.Ignite"),Transaction.CommandId,C,Skills).Code==EAetherSkillMutationCode::Applied);
    Candidate.PendingRewards.Reset();Candidate.ClaimedRewardIds.Add(Reward.RewardId);
    if(!TestTrue(TEXT("Encode complete transaction candidate"),AetherProfileCodec::Encode(Candidate,Items,Skills,Rules,Transaction.Writes[0].Value.Payload,Reason)))return false;
    Options.Fault->Store(EAetherStoreFault::AfterFirstWrite);
    TestTrue(TEXT("Injected failure rejects entire profile"),Opened.Store->Commit(Transaction).Get().Code!=EAetherStoreCode::Committed);
    Options.Fault->Store(EAetherStoreFault::None);
    auto Row=Opened.Store->Read({EAetherAggregateKind::Profile,S.CharacterId}).Get();FAetherProfileStateV10 Loaded;
    if(!TestTrue(TEXT("Decode old state after rollback"),Row.Value.IsSet()&&AetherProfileCodec::Decode(Row.Value->Payload,Items,Skills,Rules,Loaded,Reason)))return false;
    TestTrue(TEXT("No partial economy/skill/reward publication"),Loaded.Gold==100&&Loaded.Inventory.At(6)->Quantity==5&&Loaded.Skills.PermanentRank(TEXT("Fire.Ignite"))==1&&Loaded.Skills.AvailableSkillPoints==3&&Loaded.PendingRewards.Num()==1&&Loaded.ClaimedRewardIds.IsEmpty());
    TestTrue(TEXT("Retry commits all fields"),Opened.Store->Commit(Transaction).Get().Code==EAetherStoreCode::Committed);
    TestTrue(TEXT("Duplicate command replays without another payment"),Opened.Store->Commit(Transaction).Get().Code==EAetherStoreCode::Replayed);
    Opened.Store->Close();Opened.Store.Reset();Opened=AetherSQLite::Open(Options);
    if(!TestTrue(TEXT("Reopen committed profile store"),Opened.Store.IsValid()))return false;
    Row=Opened.Store->Read({EAetherAggregateKind::Profile,S.CharacterId}).Get();
    if(!TestTrue(TEXT("Recover complete DTO"),Row.Value.IsSet()&&AetherProfileCodec::Decode(Row.Value->Payload,Items,Skills,Rules,Loaded,Reason)))return false;
    TestTrue(TEXT("Row and profile commit versions agree"),Row.Value->Revision==Loaded.Revision&&Loaded.Revision==1);
    TestTrue(TEXT("Single durable rank/payment and reward claim"),Loaded.Gold==90&&Loaded.Inventory.At(6)->Quantity==4&&Loaded.Skills.PermanentRank(TEXT("Fire.Ignite"))==2&&Loaded.Skills.AvailableSkillPoints==2&&Loaded.PendingRewards.IsEmpty()&&Loaded.ClaimedRewardIds.Contains(Reward.RewardId));
    Opened.Store->Close();return true;
}
#endif
