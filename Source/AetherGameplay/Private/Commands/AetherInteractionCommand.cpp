#include "Commands/AetherInteractionCommand.h"
#include "Profile/AetherProfileCodec.h"
#include "World/AetherWorldCodec.h"
namespace
{
using K=EAetherInteractionActionKind;
using R=EAetherCommandCode;
bool EligibleStory(const FAetherSkillDefinitionV10& S,const FAetherProfileStateV10& P)
{return S.bStoryBase&&!P.Skills.StoryGrants.Contains(S.SkillId)&&(S.RequiredQuest.IsEmpty()||P.Claims.Contains(S.RequiredQuest));}
R QuestCode(EAetherQuestMutationCode C)
{
    switch(C){case EAetherQuestMutationCode::Applied:case EAetherQuestMutationCode::Unchanged:return R::Applied;
        case EAetherQuestMutationCode::Capacity:return R::Capacity;case EAetherQuestMutationCode::NotAllowed:return R::NotAllowed;default:return R::Invalid;}
}
}
EAetherCommandCode AetherInteractionCommands::AuthorizeRead(const FAetherPlayerCommand& C,const FString& Actor,const FAetherProfileCommandContext& Context)
{
    if(C.Type!=EAetherCommandType::ExecuteInteraction)return R::UnsupportedAction;
    if(C.ProtocolVersion!=AetherCommands::LatestProtocolVersion)return R::UnsupportedProtocol;
    const auto& T=Context.Interaction;
    if(!Context.bCanManageInventory||!T.bLoaded||!T.bActorCanAct)return R::NotReady;
    if(!T.TargetStableId.Equals(C.TargetStableId,ESearchCase::CaseSensitive))return R::Missing;
    if(!T.OwnerCharacterId.IsEmpty()&&!T.OwnerCharacterId.Equals(Actor,ESearchCase::CaseSensitive))return R::Unauthorized;
    if(!T.bInRange||!T.bLineOfSight)return R::OutOfReach;
    if(T.InteractionRevision!=C.ExpectedInteractionRevision)return R::StaleRevision;
    return R::Applied;
}
bool AetherInteractionCommands::Prepare(const FAetherPlayerCommand& C,const FString& Actor,const FAetherStoreSnapshotResult& S,
    const FAetherProfileCommandContext& Context,const FAetherV10ItemDefinitions& Items,const FAetherSkillDefinitionsV10& Skills,
    const FAetherRules& Rules,const FAetherEconomyDefinitionsV10& Economy,const FAetherInteractionDefinitions& Definitions,
    const FAetherQuestProgressionDefinitions& Progression,FAetherTransaction& Transaction,FAetherCommandResult& Result)
{
    Result={};Result.CommandId=C.CommandId;FString Reason;int64 ProfileRevision=-1;
    const auto Fail=[&](R Code){Result={};Result.CommandId=C.CommandId;Result.Code=Code;Result.FinalProfileRevision=ProfileRevision;return false;};
    if(!AetherCommands::Validate(C,Reason))return Fail(AetherCommands::IsSupportedProtocol(C.ProtocolVersion)?R::Invalid:R::UnsupportedProtocol);
    const auto Access=AuthorizeRead(C,Actor,Context);if(Access!=R::Applied)return Fail(Access);
    if(!Definitions.Validate(Rules,Economy,Reason)||!Progression.Validate(Rules,Reason))return Fail(R::NotReady);
    const auto* D=Definitions.Targets.Find(Context.Interaction.DefinitionId);
    if(!D||!D->Id.Equals(Context.Interaction.DefinitionId,ESearchCase::CaseSensitive))return Fail(R::Missing);
    const auto* PR=S.Values.Find({EAetherAggregateKind::Profile,Actor});const auto* WR=S.Values.Find({EAetherAggregateKind::World,TEXT("Main")});
    FAetherProfileStateV10 P;FAetherWorldStateV10 W;
    if(S.Code!=EAetherStoreCode::Found||!PR||!WR||PR->SchemaVersion!=10||WR->SchemaVersion!=10||
        !AetherProfileCodec::Decode(PR->Payload,Items,Skills,Rules,P,Reason)||!P.CharacterId.Equals(Actor,ESearchCase::CaseSensitive)||
        P.Revision!=PR->Revision||!S.ProfileRevisions.Contains(Actor)||S.ProfileRevisions[Actor]!=P.Revision||
        !AetherWorldCodec::Decode(WR->Payload,Items,Rules,S.ProfileRevisions,W,Reason)||W.Revision!=WR->Revision)return Fail(R::StorageUnavailable);
    ProfileRevision=P.Revision;
    if(P.Revision!=C.ExpectedProfileRevision||W.Revision!=C.ExpectedWorldRevision)return Fail(R::StaleRevision);
    if(P.Revision>=MAX_int64-1||W.Revision>=MAX_int64-1)return Fail(R::NotReady);
    auto Target=Context.Interaction;Target.CharacterId=Actor;Target.ProfileRevision=P.Revision;Target.WorldRevision=W.Revision;
    Target.Claims.Reset();Target.Evidence.Reset();
    for(const auto& Id:P.Claims)Target.Claims.Add(Id);for(const auto& Id:P.Evidence)Target.Evidence.Add(Id);
    Target.bHasStoryGrantAvailable=false;for(const auto& Def:Skills.Skills)Target.bHasStoryGrantAvailable|=EligibleStory(Def.Value,P);
    Target.bHasClaimableSkillPoints=AetherQuestProgression::HasClaimableSkillPoints(P,Progression);
    // 不采信上下文宣称的处理器集合；可执行范围只能来自本编译单元真实实现的有限分支。
    Target.RegisteredHandlers={K::Register,K::BindInn,K::LearnStorySkills,K::ClaimQuest,K::ClaimSkillPoints};
    FAetherInteractionProvider Provider(*D,Target,Rules);
    const auto Checked=Provider.CheckCommand(Actor,C);if(Checked!=R::Applied)return Fail(Checked);
    const auto* Action=D->Actions.FindByPredicate([&](const auto& A){return A.Id.Equals(C.ActionId,ESearchCase::CaseSensitive);});
    if(!Action)return Fail(R::Missing);
    auto Next=P;FAetherQuestMutation QuestResult;QuestResult.Code=EAetherQuestMutationCode::Unchanged;
    switch(Action->Kind)
    {
    case K::Register:case K::BindInn:
    {
        const FString Fact=Action->Kind==K::Register?TEXT("Register"):TEXT("Inn");
        if(!AetherQuestProgression::Observe(Next,Fact,Rules))return Fail(R::NotAllowed);
        Result.AffectedDefinitionIds.Add(Fact);
        QuestResult=AetherQuestProgression::Settle(Next,W.WorldFactSources,{},Items,Skills,Rules,Progression);break;
    }
    case K::ClaimQuest:
        QuestResult=AetherQuestProgression::Settle(Next,W.WorldFactSources,Action->QuestId,Items,Skills,Rules,Progression);break;
    case K::ClaimSkillPoints:
        QuestResult=AetherQuestProgression::ClaimSkillPoints(Next,Items,Skills,Rules,Progression);break;
    case K::LearnStorySkills:
    {
        TArray<FString> Keys;Skills.Skills.GetKeys(Keys);Keys.Sort();
        for(const auto& Id:Keys)
        {
            const auto& Def=Skills.Skills[Id];if(!EligibleStory(Def,Next))continue;
            const FString Source=Def.RequiredQuest.IsEmpty()?TEXT("Story.Base"):TEXT("Story.")+Def.RequiredQuest;
            const auto Granted=Next.Skills.GrantStory(Id,Source,Skills);
            if(Granted.Code!=EAetherSkillMutationCode::Applied)return Fail(R::NotAllowed);
            Result.AffectedDefinitionIds.Add(Id);
        }
        if(Result.AffectedDefinitionIds.IsEmpty())return Fail(R::NotAllowed);
        break;
    }
    default:return Fail(R::UnsupportedAction);
    }
    if(QuestCode(QuestResult.Code)!=R::Applied)return Fail(QuestCode(QuestResult.Code));
    for(const auto& Id:QuestResult.ClaimedQuests)Result.AffectedDefinitionIds.AddUnique(Id);
    for(const auto& Item:Next.Inventory.Items)
    {
        const auto* Before=P.Inventory.Find(Item.InstanceId);const int32 Added=Item.Quantity-(Before?Before->Quantity:0);
        if(Added>0){Result.AffectedIds.Add(Item.InstanceId);Result.ActualQuantity+=Added;}
    }
    if(Result.AffectedDefinitionIds.Num()>32)return Fail(R::Capacity);
    Result.ReasonParameters.Add(TEXT("SkillPointsChanged"),FString::FromInt(QuestResult.AwardedSkillPoints));
    Result.ReasonParameters.Add(TEXT("GoldChanged"),FString::FromInt(Next.Gold-P.Gold));
    Result.ReasonParameters.Add(TEXT("DeferredRewards"),FString::FromInt(QuestResult.DeferredRewards.Num()));
    Result.ReasonParameters.Add(TEXT("InteractionRevision"),LexToString(Target.InteractionRevision));
    ++Next.Revision;++W.Revision;Result.Code=R::Applied;Result.FinalProfileRevision=Next.Revision;Result.FinalWorldRevision=W.Revision;
    FAetherTransaction T;T.ActorId=Actor;T.CommandId=C.CommandId;T.ProtocolVersion=C.ProtocolVersion;T.ExpectedProfileRevision=P.Revision;
    FAetherAggregateWrite ProfileWrite,WorldWrite;ProfileWrite.ExpectedRevision=P.Revision;WorldWrite.ExpectedRevision=WR->Revision;
    ProfileWrite.Value.Key={EAetherAggregateKind::Profile,Actor};ProfileWrite.Value.Revision=Next.Revision;
    WorldWrite.Value.Key={EAetherAggregateKind::World,TEXT("Main")};WorldWrite.Value.Revision=W.Revision;
    auto Revisions=S.ProfileRevisions;Revisions[Actor]=Next.Revision;
    if(!AetherProfileCodec::Encode(Next,Items,Skills,Rules,ProfileWrite.Value.Payload,Reason)||
        !AetherWorldCodec::Encode(W,Items,Rules,Revisions,WorldWrite.Value.Payload,Reason)||
        !AetherCommands::Encode(C,T.Request,Reason)||!AetherCommands::EncodeResult(Result,T.Result,Reason))return Fail(R::Invalid);
    // 虽然本批只改个人进度，也比较并推进读取过的世界版本，确保追溯事实与奖励属于同一提交。
    T.Writes.Add(MoveTemp(ProfileWrite));T.Writes.Add(MoveTemp(WorldWrite));
    if(!AetherTransactions::Validate(T,Reason))return Fail(R::Invalid);
    Transaction=MoveTemp(T);return true;
}
