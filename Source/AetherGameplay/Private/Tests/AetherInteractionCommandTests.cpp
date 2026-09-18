#include "Misc/AutomationTest.h"
#include "Commands/AetherProfileCoordinator.h"
#include "Persistence/AetherSqliteStore.h"
#include "Profile/AetherProfileCodec.h"
#include "World/AetherWorldCodec.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherInteractionCommitTest,"Aether.V10.Interaction.DurableProgressionAndLiveRevalidation",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherInteractionCommitTest::RunTest(const FString&)
{
    using R=EAetherCommandCode;FString Json,Reason;const auto Root=FPaths::ProjectContentDir()/TEXT("AetherCore/Definitions/V10/");
    FFileHelper::LoadFileToString(Json,*(Root+TEXT("Items.json")));const auto Items=FAetherV10ItemDefinitions::Parse(Json,Reason);
    const auto& Skills=FAetherSkillDefinitionsV10::Get();const auto& Rules=FAetherRules::Get();
    FFileHelper::LoadFileToString(Json,*(Root+TEXT("Economy.json")));const auto Economy=FAetherEconomyDefinitionsV10::Parse(Json,Items,Reason);
    FFileHelper::LoadFileToString(Json,*(Root+TEXT("Interactions.json")));const auto Interactions=FAetherInteractionDefinitions::Parse(Json,Rules,Economy,Reason);
    FFileHelper::LoadFileToString(Json,*(Root+TEXT("Progression.json")));const auto Progression=FAetherQuestProgressionDefinitions::Parse(Json,Rules,Reason);
    if(!TestTrue(*Reason,Interactions.Validate(Rules,Economy,Reason)&&Progression.Validate(Rules,Reason)))return false;
    FAetherProfileStateV10 P;P.CharacterId=TEXT("Alice");P.Claims={TEXT("Q_Main_01")};P.Evidence={TEXT("SupplyA"),TEXT("SupplyB"),TEXT("Gate")};P.Gold=40;P.Experience=100;
    for(int32 I=0;I<32;++I){FAetherV10ItemInstance Item;Item.InstanceId=FGuid::NewGuid();Item.DefinitionId=TEXT("Potion");Item.Quantity=20;Item.SlotIndex=I;P.Inventory.Items.Add(Item);}
    FAetherWorldStateV10 World;
    FAetherSqliteOptions O;O.DatabasePath=FPaths::ProjectSavedDir()/TEXT("Automation/V10Interactions")/FGuid::NewGuid().ToString(EGuidFormats::Digits)/TEXT("state.sqlite");
    O.Fault=MakeShared<TAtomic<EAetherStoreFault>,ESPMode::ThreadSafe>(EAetherStoreFault::None);
    auto DB=AetherSQLite::Open(O);if(!TestTrue(TEXT("Open isolated interaction database"),DB.Store.IsValid()))return false;
    // 夹具的内部服务器事实写入使用独立请求字节；客户端没有这个测试专用入口。
    const auto WriteProfile=[&](const FAetherProfileStateV10& Value,bool IncludeWorld)
    {
        FAetherTransaction T;T.ActorId=Value.CharacterId;T.ExpectedProfileRevision=Value.Revision-1;T.CommandId=AetherTransactions::NewCommandId(T.ExpectedProfileRevision);T.Request={9};
        FAetherAggregateWrite Row;Row.ExpectedRevision=Value.Revision-1;Row.Value.Key={EAetherAggregateKind::Profile,Value.CharacterId};Row.Value.Revision=Value.Revision;
        AetherProfileCodec::Encode(Value,Items,Skills,Rules,Row.Value.Payload,Reason);T.Writes.Add(Row);
        if(IncludeWorld){FAetherAggregateWrite W;W.Value.Key={EAetherAggregateKind::World,TEXT("Main")};
            AetherWorldCodec::Encode(World,Items,Rules,{{Value.CharacterId,Value.Revision}},W.Value.Payload,Reason);T.Writes.Add(W);}
        return DB.Store->Commit(T).Get().Code==EAetherStoreCode::Committed;
    };
    if(!TestTrue(TEXT("Seed profile and consistent world"),WriteProfile(P,true))){DB.Store->Close();return false;}
    FAetherProfileCoordinator Service(DB.Store.ToSharedRef(),Items,Skills,Rules,Economy,Interactions,Progression);
    auto Session=Service.BeginSession(TEXT("Alice"));FAetherProfileCommandContext Context;Context.bCanManageInventory=true;
    const auto Target=[&](const TCHAR* Definition,const TCHAR* Id,int64 Revision)
    {
        Context.Interaction={};auto& T=Context.Interaction;T.DefinitionId=Definition;T.TargetStableId=Id;T.InteractionRevision=Revision;
        T.bLoaded=T.bInRange=T.bLineOfSight=T.bActorCanAct=true;
        // 故意把非当前角色的“全完成”缓存放进上下文，领域必须以已读 Profile 覆盖。
        for(const auto& Q:Rules.Quests)T.Claims.Add(Q.Id.ToString());
        T.bHasStoryGrantAvailable=T.bHasClaimableSkillPoints=true;T.RegisteredHandlers.Add(EAetherInteractionActionKind::Train);
    };
    int32 Resolved=0;bool UnloadSecond=false,ChangeDefinition=false;
    const FAetherResolveProfileContext Resolve=[&](const auto&,const auto&,const auto&,auto& Out)
    {Out=Context;++Resolved;if(Resolved==2&&UnloadSecond)Out.Interaction.bLoaded=false;if(Resolved==2&&ChangeDefinition)Out.Interaction.DefinitionId=TEXT("Inn");return true;};
    const auto Command=[&](const TCHAR* Action,int64 Profile,int64 W)
    {
        FAetherPlayerCommand C;C.Type=EAetherCommandType::ExecuteInteraction;C.ProtocolVersion=2;
        C.CommandId=AetherTransactions::NewCommandId(Profile);C.ExpectedProfileRevision=Profile;C.ExpectedWorldRevision=W;
        C.TargetStableId=Context.Interaction.TargetStableId;C.ExpectedInteractionRevision=Context.Interaction.InteractionRevision;C.ActionId=Action;return C;
    };
    FAetherProfileCompletion Done;
    const auto Send=[&](const FAetherPlayerCommand& C)
    {
        FAetherCommandResult Reject;Resolved=0;
        if(!Service.Submit(Session,C,Reject)){Done={};Done.Result=Reject;return Reject.Code;}
        const double End=FPlatformTime::Seconds()+5;
        while(FPlatformTime::Seconds()<End)
        {
            auto Results=Service.Poll(Resolve);if(!Results.IsEmpty()){Done=MoveTemp(Results[0]);return Done.Result.Code;}FPlatformProcess::Sleep(.001f);
        }
        Done={};return R::Busy;
    };
    const auto ReadProfile=[&](const FString& Id,FAetherProfileStateV10& Out)
    {const auto Row=DB.Store->Read({EAetherAggregateKind::Profile,Id}).Get();return Row.Value.IsSet()&&AetherProfileCodec::Decode(Row.Value->Payload,Items,Skills,Rules,Out,Reason);};
    Target(TEXT("Teacher"),TEXT("Town.Teacher"),30);
    TestTrue(TEXT("Forged cached quest completion cannot grant story skills"),Send(Command(TEXT("LearnStorySkills"),0,0))==R::Missing);
    Target(TEXT("Register"),TEXT("Town.Register"),10);auto Register=Command(TEXT("Register"),0,0);
    TestTrue(TEXT("Registration commits through coordinator"),Send(Register)==R::Applied);
    if(!TestTrue(TEXT("Committed profile and world publish together"),Done.Snapshot.IsSet()&&Done.WorldSnapshot.IsSet())){DB.Store->Close();return false;}
    TestTrue(TEXT("Only actual registration evidence was added"),Done.Snapshot->Revision==1&&Done.WorldSnapshot->Revision==1&&Done.Snapshot->Evidence.Contains(TEXT("Register"))&&!Done.Snapshot->bRegistered&&!Done.Snapshot->Claims.Contains(TEXT("Q_Main_02")));
    TestTrue(TEXT("Registration replay bypasses current evaluator"),Send(Register)==R::Replayed&&Resolved==0&&Done.Snapshot->Revision==1);
    Target(TEXT("Inn"),TEXT("Town.Inn"),20);auto Bind=Command(TEXT("BindInn"),1,1);
    O.Fault->Store(EAetherStoreFault::AfterFirstWrite);TestTrue(TEXT("Binding storage failure is explicit"),Send(Bind)==R::StorageUnavailable&&!Done.Snapshot.IsSet());
    FAetherProfileStateV10 Stored;ReadProfile(TEXT("Alice"),Stored);
    TestTrue(TEXT("Failed two-aggregate binding rolls back both"),Stored.Revision==1&&!Stored.bRegistered&&DB.Store->Read({EAetherAggregateKind::World,TEXT("Main")}).Get().Value->Revision==1);
    TestTrue(TEXT("Same bind request can retry after rollback"),Send(Bind)==R::Applied);
    TestTrue(TEXT("Full bag binding preserves whole reward as pending"),Done.Snapshot->Revision==2&&Done.WorldSnapshot->Revision==2&&Done.Snapshot->bRegistered&&Done.Snapshot->PendingRewards.Num()==1&&Done.Snapshot->Gold==40&&Done.Snapshot->Experience==200);
    Target(TEXT("Teacher"),TEXT("Town.Teacher"),30);auto Learn=Command(TEXT("LearnStorySkills"),2,2);
    TestTrue(TEXT("Teacher grants actual stable story skill identities"),Send(Learn)==R::Applied&&Done.Snapshot->Skills.PermanentRank(TEXT("Fire.Ignite"))==1&&Done.Snapshot->Skills.PermanentRank(TEXT("Water.Draw"))==1&&Done.Snapshot->Skills.Purchases.IsEmpty());
    TestTrue(TEXT("Repeat story grant preserves version and sources"),Send(Learn)==R::Replayed&&Resolved==0&&Done.Snapshot->Revision==3&&Done.Snapshot->Skills.StoryGrants.Num()==2);
    TestTrue(TEXT("No fake success for training without spawn adapter"),Send(Command(TEXT("Train"),3,3))==R::UnsupportedAction);
    UnloadSecond=true;TestTrue(TEXT("Target unload during consistent snapshot read blocks write"),Send(Command(TEXT("ClaimTraining"),3,3))==R::NotReady&&Resolved==2);UnloadSecond=false;
    ChangeDefinition=true;TestTrue(TEXT("Target configuration swap cannot change action meaning"),Send(Command(TEXT("ClaimTraining"),3,3))==R::Conflict&&Resolved==2);ChangeDefinition=false;
    TestTrue(TEXT("Expected world version is rechecked"),Send(Command(TEXT("ClaimTraining"),3,2))==R::StaleRevision);
    auto Old=Command(TEXT("ClaimTraining"),3,3);Old.ProtocolVersion=1;Old.ExpectedInteractionRevision=-1;
    TestTrue(TEXT("Old protocol cannot execute target-version-free interaction"),Send(Old)==R::UnsupportedProtocol);
    ReadProfile(TEXT("Alice"),P);
    for(const auto* Fact:{TEXT("Melee1"),TEXT("Melee2"),TEXT("Melee3"),TEXT("Block"),TEXT("TrainingExtinguished")})AetherQuestProgression::Observe(P,Fact,Rules);
    ++P.Revision;TestTrue(TEXT("Seed server-verified combat evidence"),WriteProfile(P,false));
    auto Claim=Command(TEXT("ClaimTraining"),4,3);O.Fault->Store(EAetherStoreFault::AfterCommitBeforeReply);
    TestTrue(TEXT("Lost quest response cannot publish candidate"),Send(Claim)==R::StorageUnavailable&&!Done.Snapshot.IsSet());
    TestTrue(TEXT("Quest retry recovers committed points and reward once"),Send(Claim)==R::Replayed&&Resolved==0&&Done.Snapshot->Revision==5&&Done.WorldSnapshot->Revision==4&&
        Done.Snapshot->Skills.AvailableSkillPoints==2&&Done.Snapshot->Gold==120&&Done.Snapshot->Experience==300);
    // 已有老角色通过明确的新入口领取成长点数；不是给历史学习虚构付费/退款账本。
    FAetherProfileStateV10 Legacy;Legacy.CharacterId=TEXT("Bob");Legacy.Gold=123;Legacy.Experience=800;
    for(const auto& Q:Rules.Quests)Legacy.Claims.Add(Q.Id.ToString());FAetherSkillStateV10::FromLegacyMask(15,Skills,Legacy.Skills,Reason);
    TestTrue(TEXT("Seed synthetic completed legacy player"),WriteProfile(Legacy,false));
    Session=Service.BeginSession(TEXT("Bob"));auto Points=Command(TEXT("ClaimSkillPoints"),0,4);
    TestTrue(TEXT("Explicit teacher action awards completed quest sources"),Send(Points)==R::Applied&&Done.Snapshot->Skills.AvailableSkillPoints==12&&Done.Snapshot->Skills.PointEvents.Num()==6&&Done.Snapshot->Skills.Purchases.IsEmpty()&&Done.Snapshot->Gold==123&&Done.Snapshot->Experience==800);
    TestTrue(TEXT("Point action replay cannot duplicate awards"),Send(Points)==R::Replayed&&Done.Snapshot->Revision==1&&Done.Snapshot->Skills.AvailableSkillPoints==12);
    TestTrue(TEXT("New request hides settled point claim"),Send(Command(TEXT("ClaimSkillPoints"),1,5))==R::Missing);
    ReadProfile(TEXT("Alice"),Stored);TestTrue(TEXT("Other player's claim cannot alter Alice sources"),Stored.Revision==5&&Stored.Skills.AvailableSkillPoints==2);
    DB.Store->Close();return true;
}
#endif
