#include "Misc/AutomationTest.h"
#include "Commands/AetherProfileCommand.h"
#include "Commands/AetherConsumableDeliveryPump.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Profile/AetherProfileCodec.h"
#include "Persistence/AetherSqliteStore.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#if WITH_DEV_AUTOMATION_TESTS
namespace
{
struct FCase
{
    FAetherV10ItemDefinitions Items;
    FAetherProfileStateV10 Profile;
    FAetherPlayerCommand Command;
    FAetherProfileCommandContext Context;
    FAetherTransaction Transaction;
    FAetherCommandResult Result;
    FCase()
    {
        FString Json,Reason;FFileHelper::LoadFileToString(Json,*(FPaths::ProjectContentDir()/TEXT("AetherCore/Definitions/V10/Items.json")));
        Items=FAetherV10ItemDefinitions::Parse(Json,Reason);Profile.CharacterId=TEXT("Alice");
        FAetherV10ItemInstance I;I.InstanceId=FGuid(9,8,7,6);I.DefinitionId=TEXT("Potion");I.Quantity=2;I.SlotIndex=3;
        I.Quality=2;I.Affixes.Add(TEXT("Recovery"),2);I.StateGroup=TEXT("Wet");I.bFavorite=true;I.BoundToCharacter=Profile.CharacterId;
        Profile.Inventory.Items.Add(I);
        Command.Type=EAetherCommandType::UseItem;Command.ExpectedProfileRevision=0;Command.CommandId=AetherTransactions::NewCommandId(0);Command.ItemInstanceId=I.InstanceId;
        Context.bCanManageInventory=true;Context.ResourceReservationId=Command.CommandId;
        Context.Resources.LifeId=FGuid(1,2,3,4);Context.Resources.Revision=7;Context.Resources.Health=20;
        Context.ServerUnixMs=1700000000000LL;Context.SafeForSeconds=60;
    }
    bool Prepare(){return AetherProfileCommands::Prepare(Command,Profile.CharacterId,Profile,Context,Items,FAetherSkillDefinitionsV10::Get(),FAetherRules::Get(),Transaction,Result);}
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherConsumableRulesTest,"Aether.V10.Consumables.AuthorityAndVersionedEffect",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherConsumableRulesTest::RunTest(const FString&)
{
    FCase C;FString Reason;
    if(!TestTrue(TEXT("Bound owner consumes one potion"),C.Prepare()))return false;
    TestEqual(TEXT("Exactly one durable effect"),C.Transaction.Effects.Num(),1);
    if(C.Transaction.Effects.Num()!=1)return false;
    const auto Delivery=C.Transaction.Effects[0];FAetherConsumableEffectV10 E;
    TestTrue(TEXT("Versioned absolute result decodes"),AetherConsumableEffects::Decode(Delivery.Payload,E));
    const auto* Use=FAetherRules::Get().Uses.Find(FName(*C.Items.Items[TEXT("Potion")].UseId));
    TestTrue(TEXT("Authoritative amount, life and resource revision"),Use&&E.After.Health==FMath::Min(100.0,20+Use->Health)&&E.After.Revision==8&&E.Before.LifeId==E.After.LifeId);
    FAetherProfileStateV10 Next;AetherProfileCodec::Decode(C.Transaction.Writes[0].Value.Payload,C.Items,FAetherSkillDefinitionsV10::Get(),FAetherRules::Get(),Next,Reason);
    TestTrue(TEXT("Remaining stack keeps identity and all metadata"),Next.Revision==1&&Next.Inventory.Find(C.Command.ItemInstanceId)->Quantity==1&&
        Next.Inventory.Find(C.Command.ItemInstanceId)->SameStackKey(C.Profile.Inventory.Items[0]));
    const auto Fail=[&](const TCHAR* Why)
    {
        C.Transaction.ActorId=TEXT("Sentinel");TestFalse(Why,C.Prepare());
        TestTrue(TEXT("Failure clears speculative receipt and preserves output transaction"),C.Transaction.ActorId==TEXT("Sentinel")&&C.Result.ActualQuantity==0&&C.Result.AffectedIds.IsEmpty()&&C.Result.ReasonParameters.IsEmpty());
    };
    C.Context.ResourceReservationId.Invalidate();Fail(TEXT("Cannot use without resource reservation"));C.Context.ResourceReservationId=C.Command.CommandId;
    C.Context.Resources.Health=100;Fail(TEXT("Full health does not waste potion"));C.Context.Resources.Health=20;
    C.Context.Resources.Health=0;Fail(TEXT("Dead pawn cannot use"));C.Context.Resources.Health=20;
    C.Context.Resources.UseReadyAtUnixMs=C.Context.ServerUnixMs+1;Fail(TEXT("Cooldown is authoritative"));C.Context.Resources.UseReadyAtUnixMs=0;
    C.Profile.Inventory.Items[0].bLocked=true;Fail(TEXT("Locked item cannot be consumed"));C.Profile.Inventory.Items[0].bLocked=false;
    C.Profile.Inventory.Items[0].BoundToCharacter=TEXT("Bob");Fail(TEXT("Foreign binding cannot be consumed"));C.Profile.Inventory.Items[0].BoundToCharacter=TEXT("Alice");
    C.Profile.Inventory.Items[0].DefinitionId=TEXT("Ration");C.Context.SafeForSeconds=0;Fail(TEXT("Food requires configured safe time"));
    C.Context.SafeForSeconds=60;TestTrue(TEXT("Food can be used after safe time"),C.Prepare());
    // 编码不接受截断/尾随数据/未知版本；失败不能清空调用者持有的恢复记录。
    for(int32 N=0;N<Delivery.Payload.Num();++N)
    {
        auto Truncated=Delivery.Payload;Truncated.SetNum(N);auto Out=E;
        if(AetherConsumableEffects::Decode(Truncated,Out)||Out.DeliveryId!=E.DeliveryId){AddError(TEXT("Truncation changed output"));break;}
    }
    auto Bad=Delivery.Payload;Bad.Add(0);TestFalse(TEXT("Trailing bytes rejected"),AetherConsumableEffects::Decode(Bad,E));
    Bad=Delivery.Payload;Bad[4]=2;TestFalse(TEXT("Unknown schema rejected"),AetherConsumableEffects::Decode(Bad,E));
    auto Invalid=E;Invalid.After.Health=Invalid.Before.Health-1;TestFalse(TEXT("A recovery record cannot inflict damage"),AetherConsumableEffects::Encode(Invalid,Bad));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherConsumableRecoveryTest,"Aether.V10.Consumables.RollbackLostReplyAndRespawn",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherConsumableRecoveryTest::RunTest(const FString&)
{
    using A=EAetherEffectApplyCode;FCase C;FString Reason;
    FAetherSqliteOptions O;O.DatabasePath=FPaths::ProjectSavedDir()/TEXT("Automation/V10Consumables")/FGuid::NewGuid().ToString(EGuidFormats::Digits)/TEXT("state.sqlite");
    O.Fault=MakeShared<TAtomic<EAetherStoreFault>,ESPMode::ThreadSafe>(EAetherStoreFault::None);
    auto DB=AetherSQLite::Open(O);if(!TestTrue(TEXT("Open isolated consumable DB"),DB.Store.IsValid()))return false;
    FAetherTransaction Seed;Seed.ActorId=C.Profile.CharacterId;Seed.ExpectedProfileRevision=-1;Seed.CommandId=AetherTransactions::NewCommandId(-1);Seed.Request={1};
    FAetherAggregateWrite Row;Row.Value.Key={EAetherAggregateKind::Profile,C.Profile.CharacterId};
    AetherProfileCodec::Encode(C.Profile,C.Items,FAetherSkillDefinitionsV10::Get(),FAetherRules::Get(),Row.Value.Payload,Reason);Seed.Writes.Add(Row);
    TestTrue(TEXT("Seed profile"),DB.Store->Commit(Seed).Get().Code==EAetherStoreCode::Committed);
    const auto ReadProfile=[&](FAetherProfileStateV10& P)
    {
        const auto R=DB.Store->Read(Row.Value.Key).Get();
        return R.Value.IsSet()&&AetherProfileCodec::Decode(R.Value->Payload,C.Items,FAetherSkillDefinitionsV10::Get(),FAetherRules::Get(),P,Reason);
    };
    FAetherConsumableReceiver Receiver(TEXT("Alice"),C.Context.Resources);
    TestTrue(TEXT("Reserve exact pre-commit resource state"),Receiver.Reserve(C.Command.CommandId,C.Context.Resources));
    auto Damage=C.Context.Resources;++Damage.Revision;Damage.Health=1;
    TestFalse(TEXT("Pending transaction gates resource update; caller must defer whole action"),Receiver.UpdateResources(Damage));
    if(!TestTrue(TEXT("Prepare consumption"),C.Prepare())){DB.Store->Close();return false;}
    O.Fault->Store(EAetherStoreFault::AfterFirstWrite);
    TestTrue(TEXT("Injected failure rolls back"),DB.Store->Commit(C.Transaction).Get().Code==EAetherStoreCode::Unavailable);
    FAetherProfileStateV10 P;TestTrue(TEXT("Rollback preserves item and leaves no delivery"),ReadProfile(P)&&P.Revision==0&&P.Inventory.Items[0].Quantity==2&&DB.Store->PendingEffects(TEXT("Alice")).Get().Values.IsEmpty());
    O.Fault->Store(EAetherStoreFault::AfterCommitBeforeReply);
    TestTrue(TEXT("Commit response lost"),DB.Store->Commit(C.Transaction).Get().Code==EAetherStoreCode::Unavailable);
    TestTrue(TEXT("Unknown commit keeps resource gate"),Receiver.IsReserved(C.Command.CommandId));
    DB.Store->Close();DB.Store.Reset();DB=AetherSQLite::Open(O);
    if(!TestTrue(TEXT("Reopen after lost commit response"),DB.Store.IsValid()))return false;
    TestTrue(TEXT("Receipt recovers without consuming twice"),DB.Store->Commit(C.Transaction).Get().Code==EAetherStoreCode::Replayed);
    TestTrue(TEXT("Committed item count and version recover"),ReadProfile(P)&&P.Revision==1&&P.Inventory.Items[0].Quantity==1);
    auto Pending=DB.Store->PendingEffects(TEXT("Alice")).Get();
    if(!TestTrue(TEXT("One committed pending effect"),Pending.Code==EAetherStoreCode::Found&&Pending.Values.Num()==1)){DB.Store->Close();return false;}
    const auto D=Pending.Values[0];FAetherConsumableEffectV10 E;AetherConsumableEffects::Decode(D.Payload,E);
    TestTrue(TEXT("Wrong owner cannot receive"),Receiver.Apply(D,TEXT("Bob"))==A::Invalid);
    TestTrue(TEXT("Committed target restores once"),Receiver.Apply(D,TEXT("Alice"))==A::Applied&&Receiver.State().Same(E.After));
    Damage=Receiver.State();++Damage.Revision;Damage.Health=5;
    TestTrue(TEXT("Damage after committed healing is accepted"),Receiver.UpdateResources(Damage));
    TestTrue(TEXT("Lost delivery acknowledgement cannot heal again after damage"),Receiver.Apply(D,TEXT("Alice"))==A::Replayed&&Receiver.State().Health==5);
    auto Changed=E;Changed.After.Health+=1;auto Forged=D;AetherConsumableEffects::Encode(Changed,Forged.Payload);
    TestTrue(TEXT("Reused delivery ID with another valid result conflicts"),Receiver.Apply(Forged,TEXT("Alice"))==A::Conflict);
    FAetherConsumableReceiver Stale(TEXT("Alice"),Damage);
    TestTrue(TEXT("No dedupe proof and newer resource version cannot overwrite damage"),Stale.Apply(D,TEXT("Alice"))==A::Conflict);
    // 新进程丢失内存去重表时，必须经过显式的全资源新生命恢复，不可直接重放旧治疗。
    auto Fresh=C.Context.Resources;Fresh.LifeId=FGuid::NewGuid();Fresh.Revision=0;Fresh.Health=Fresh.MaxHealth;
    FAetherConsumableReceiver Respawn(TEXT("Alice"),Fresh);
    TestTrue(TEXT("Old life cannot target new pawn through ordinary apply"),Respawn.Apply(D,TEXT("Alice"))==A::Conflict);
    auto Duplicate=Pending.Values;Duplicate.Add(D);
    TestFalse(TEXT("Corrupt duplicate recovery batch changes nothing"),Respawn.RecoverAtFullRespawn(Duplicate,TEXT("Alice")));
    TestTrue(TEXT("Full respawn resolves old pending restore before input opens"),Respawn.RecoverAtFullRespawn(Pending.Values,TEXT("Alice"))&&Respawn.State().Health==100&&Respawn.State().UseReadyAtUnixMs==E.After.UseReadyAtUnixMs);
    Damage=Respawn.State();++Damage.Revision;Damage.Health=6;Respawn.UpdateResources(Damage);
    TestTrue(TEXT("Old delivery after new-life damage is still idempotent"),Respawn.Apply(D,TEXT("Alice"))==A::Replayed&&Respawn.State().Health==6);
    TestTrue(TEXT("Acknowledge only after recovery"),DB.Store->AcknowledgeEffect(TEXT("Alice"),D.Id).Get());
    TestTrue(TEXT("Release acknowledged in-memory receipt"),Respawn.ForgetAcknowledged(D.Id));
    TestTrue(TEXT("Late stale batch after ACK cannot heal"),Respawn.Apply(D,TEXT("Alice"))==A::Conflict&&Respawn.State().Health==6);
    DB.Store->Close();DB.Store.Reset();DB=AetherSQLite::Open(O);
    TestTrue(TEXT("Acknowledged effect stays removed after reopen"),DB.Store.IsValid()&&DB.Store->PendingEffects(TEXT("Alice")).Get().Values.IsEmpty());
    if(DB.Store.IsValid())DB.Store->Close();return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherConsumablePumpTest,"Aether.V10.Consumables.AsyncDeliveryAndEpoch",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherConsumablePumpTest::RunTest(const FString&)
{
    using R=EAetherDeliveryPumpCode;FCase C;FString Reason;
    FAetherSqliteOptions O;O.DatabasePath=FPaths::ProjectSavedDir()/TEXT("Automation/V10ConsumablePump")/FGuid::NewGuid().ToString(EGuidFormats::Digits)/TEXT("state.sqlite");
    auto DB=AetherSQLite::Open(O);if(!TestTrue(TEXT("Open isolated pump DB"),DB.Store.IsValid()))return false;
    FAetherTransaction Seed;Seed.ActorId=C.Profile.CharacterId;Seed.ExpectedProfileRevision=-1;Seed.CommandId=AetherTransactions::NewCommandId(-1);Seed.Request={1};
    FAetherAggregateWrite Row;Row.Value.Key={EAetherAggregateKind::Profile,C.Profile.CharacterId};
    AetherProfileCodec::Encode(C.Profile,C.Items,FAetherSkillDefinitionsV10::Get(),FAetherRules::Get(),Row.Value.Payload,Reason);Seed.Writes.Add(Row);
    DB.Store->Commit(Seed).Get();
    FAetherConsumableReceiver Receiver(TEXT("Alice"),C.Context.Resources);
    FAetherProfileCoordinator Coordinator(DB.Store.ToSharedRef(),C.Items,FAetherSkillDefinitionsV10::Get(),FAetherRules::Get());
    auto Session=Coordinator.BeginSession(TEXT("Alice"));FAetherCommandResult Reject;
    const FAetherResolveProfileContext Context=[&](const auto&,const auto& Cmd,const auto&,auto& Out)
    {
        Out=C.Context;Out.ResourceReservationId=Cmd.CommandId;return Receiver.Reserve(Cmd.CommandId,Out.Resources);
    };
    TestTrue(TEXT("UseItem accepted by actual async command coordinator"),Coordinator.Submit(Session,C.Command,Reject));
    TArray<FAetherProfileCompletion> Done;double Deadline=FPlatformTime::Seconds()+5;
    while(Done.IsEmpty()&&FPlatformTime::Seconds()<Deadline){Done=Coordinator.Poll(Context);FPlatformProcess::Sleep(.001f);}
    if(!TestTrue(TEXT("Coordinator commits only inventory and durable delivery before applying resources"),
        Done.Num()==1&&Done[0].Result.Code==EAetherCommandCode::Applied&&Done[0].Snapshot.IsSet()&&Receiver.State().Health==20&&Receiver.IsReserved(C.Command.CommandId)))
    {DB.Store->Close();return false;}
    C.Profile=Done[0].Snapshot.GetValue();
    FAetherConsumableReceiver* ActiveReceiver=&Receiver;
    const FAetherResolveConsumableReceiver Resolve=[&](const auto& S){return S==Session?ActiveReceiver:nullptr;};
    const auto Wait=[&](FAetherConsumableDeliveryPump& Pump)
    {
        FAetherDeliveryPumpResult Result;const double End=FPlatformTime::Seconds()+5;
        do {Result=Pump.Poll(Resolve);if(Result.Code!=R::Pending)return Result;FPlatformProcess::Sleep(.001f);}while(FPlatformTime::Seconds()<End);
        return Result;
    };
    FAetherConsumableDeliveryPump Pump(DB.Store.ToSharedRef());
    TestTrue(TEXT("Start bounded delivery batch"),Pump.Start(Session));
    TestFalse(TEXT("Do not overlap read batches"),Pump.Start(Session));
    Pump.Poll(Resolve);DB.Store->PendingEffects(TEXT("Alice")).Get(); // 排在内部读取后，建立确定的测试时序。
    TestTrue(TEXT("Apply committed effect and enqueue acknowledgement"),Pump.Poll(Resolve).Code==R::Pending&&Receiver.State().Health>20);
    TestTrue(TEXT("ACK may already reach disk before game-thread callback"),DB.Store->PendingEffects(TEXT("Alice")).Get().Values.IsEmpty());
    Session=Coordinator.BeginSession(TEXT("Alice"));
    TestTrue(TEXT("Old session cannot finish against replacement session"),Pump.Poll(Resolve).Code==R::StaleSession);
    TestEqual(TEXT("Dropped ACK reply preserves in-memory evidence"),Receiver.PendingAcknowledgementIds().Num(),1);
    auto Damage=Receiver.State();++Damage.Revision;Damage.Health=5;Receiver.UpdateResources(Damage);
    TestTrue(TEXT("New session can reconcile completed acknowledgement"),Pump.Start(Session)&&Wait(Pump).Code==R::Complete&&Receiver.PendingAcknowledgementIds().IsEmpty()&&Receiver.State().Health==5);
    FAetherConsumableReceiver Foreign(TEXT("Bob"),Receiver.State());ActiveReceiver=&Foreign;
    Pump.Start(Session);TestTrue(TEXT("Resolver cannot route Alice deliveries into Bob resources"),Pump.Poll(Resolve).Code==R::StaleSession);
    ActiveReceiver=&Receiver;
    // 再消费一件，为更换资源所有者、坏批次与新生命恢复提供真实持久投递。
    C.Command.ExpectedProfileRevision=C.Profile.Revision;C.Command.CommandId=AetherTransactions::NewCommandId(C.Profile.Revision);
    C.Context.ResourceReservationId=C.Command.CommandId;C.Context.ServerUnixMs+=10000;
    Receiver.Reserve(C.Command.CommandId,C.Context.Resources);
    if(!TestTrue(TEXT("Prepare second use"),C.Prepare())){DB.Store->Close();return false;}
    TestTrue(TEXT("Commit second use"),DB.Store->Commit(C.Transaction).Get().Code==EAetherStoreCode::Committed);
    Pump.Start(Session);Pump.Poll(Resolve);
    FAetherConsumableReceiver Other(TEXT("Alice"),C.Context.Resources);ActiveReceiver=&Other;
    TestTrue(TEXT("Same session cannot silently swap resource owner during async read"),Pump.Poll(Resolve).Code==R::StaleSession);
    TestEqual(TEXT("Stale receiver leaves delivery durable"),DB.Store->PendingEffects(TEXT("Alice")).Get().Values.Num(),1);
    auto Wrong=C.Context.Resources;++Wrong.Revision;Wrong.Health=1;FAetherConsumableReceiver WrongReceiver(TEXT("Alice"),Wrong);ActiveReceiver=&WrongReceiver;
    Pump.Start(Session);TestTrue(TEXT("Resource version conflict does not acknowledge effect"),Wait(Pump).Code==R::Conflict);
    TestEqual(TEXT("Conflicted effect remains pending"),DB.Store->PendingEffects(TEXT("Alice")).Get().Values.Num(),1);
    // 在合法记录后追加坏记录，验证不是应用前半批之后才发现坏尾项。
    auto Invalid=C.Transaction;Invalid.CommandId=AetherTransactions::NewCommandId(2);Invalid.ExpectedProfileRevision=2;Invalid.Request={99};Invalid.Result={1};
    Invalid.Writes[0].ExpectedRevision=2;Invalid.Writes[0].Value.Revision=3;
    FAetherProfileStateV10 Next;AetherProfileCodec::Decode(C.Transaction.Writes[0].Value.Payload,C.Items,FAetherSkillDefinitionsV10::Get(),FAetherRules::Get(),Next,Reason);
    Next.Revision=3;AetherProfileCodec::Encode(Next,C.Items,FAetherSkillDefinitionsV10::Get(),FAetherRules::Get(),Invalid.Writes[0].Value.Payload,Reason);
    Invalid.Effects[0].Id=Invalid.CommandId;Invalid.Effects[0].Payload={1,2,3};
    TestTrue(TEXT("Seed opaque malformed effect in isolated DB"),DB.Store->Commit(Invalid).Get().Code==EAetherStoreCode::Committed);
    ActiveReceiver=&Receiver;Pump.Start(Session);TestTrue(TEXT("Whole batch validates before any resource change"),Wait(Pump).Code==R::Invalid&&Receiver.State().Health==5);
    TestEqual(TEXT("Invalid tail leaves both records"),DB.Store->PendingEffects(TEXT("Alice")).Get().Values.Num(),2);
    // 仅测试夹具修复上述人工坏记录；生产路径绝不这样跳过未解析效果。
    DB.Store->AcknowledgeEffect(TEXT("Alice"),Invalid.CommandId).Get();
    auto Fresh=C.Context.Resources;Fresh.LifeId=FGuid::NewGuid();Fresh.Revision=0;Fresh.Health=Fresh.MaxHealth;
    FAetherConsumableReceiver Respawn(TEXT("Alice"),Fresh);ActiveReceiver=&Respawn;Session=Coordinator.ReplacePawn(Session);
    TestTrue(TEXT("New-life recovery batch starts"),Pump.Start(Session,true));
    const auto Recovered=Wait(Pump);
    TestTrue(TEXT("Explicit full respawn resolves and acknowledges old-life record"),Recovered.Code==R::Complete&&Recovered.bFullRespawnRecovered&&Recovered.Acknowledged==1&&Respawn.State().Health==100);
    TestTrue(TEXT("Successful batch removed only applied deliveries"),DB.Store->PendingEffects(TEXT("Alice")).Get().Values.IsEmpty());
    DB.Store->Close();return true;
}
#endif
