#include "Interaction/AetherInteractionDefinitions.h"
namespace
{
bool StableId(const FString& S)
{
    if(S.IsEmpty()||S.Len()>96)return false;
    for(TCHAR C:S)if(!((C>='a'&&C<='z')||(C>='A'&&C<='Z')||(C>='0'&&C<='9')||C=='_'||C=='-'||C=='.'))return false;
    return true;
}
bool ValidSnapshot(const FAetherInteractionSnapshot& S)
{
    return !S.CharacterId.IsEmpty()&&S.CharacterId.Len()<=32&&StableId(S.TargetStableId)&&StableId(S.DefinitionId)&&
        S.ProfileRevision>=0&&S.ProfileRevision<MAX_int64&&S.WorldRevision>=0&&S.WorldRevision<MAX_int64&&
        S.InteractionRevision>=0&&S.InteractionRevision<MAX_int64&&S.Claims.Num()<=512&&S.Evidence.Num()<=512;
}
bool AvailableQuest(const FAetherInteractionSnapshot& S,const FAetherQuestRule& Q)
{
    if(S.Claims.Contains(Q.Id.ToString()))return false;
    for(const auto& Id:Q.Prerequisites)if(!S.Claims.Contains(Id.ToString()))return false;return true;
}
bool HasAny(const TSet<FString>& Values,const TArray<FString>& Conditions)
{for(const auto& V:Conditions)if(Values.Contains(V))return true;return false;}
FString Missing(const TSet<FString>& Values,const TArray<FString>& Conditions)
{for(const auto& V:Conditions)if(!Values.Contains(V))return V;return {};}
}
FAetherObjectiveGuidance AetherInteractionQueries::Guidance(const FAetherInteractionSnapshot& S,const FAetherRules& Rules,const FString& Preferred)
{
    FAetherObjectiveGuidance G;if(!Rules.bValid)return G;const FAetherQuestRule* Selected=nullptr;
    for(const auto& Q:Rules.Quests)if(AvailableQuest(S,Q))
    {
        if(!Selected)Selected=&Q;if(Q.Id.ToString()==Preferred){Selected=&Q;break;}
    }
    if(!Selected)return G;
    G.QuestId=Selected->Id.ToString();
    for(const auto& Id:Selected->Objectives)if(!S.Evidence.Contains(Id.ToString()))
    {
        const auto* O=Rules.Objectives.Find(Id);if(!O)return {};
        G.ObjectiveId=Id.ToString();G.Label=O->Label;G.Hint=O->Hint;
        G.AnchorId=O->Anchor.IsNone()?FString():O->Anchor.ToString();G.PersistentPosition=O->Position;G.bHasTarget=true;return G;
    }
    G.bRewardReady=true;G.Label=TEXT("目标已完成，奖励待领取");G.Hint=TEXT("查看任务奖励并领取。");return G;
}
FAetherInteractionProvider::FAetherInteractionProvider(FAetherInteractionDefinition D,FAetherInteractionSnapshot S,FAetherRules R)
    :Definition(MoveTemp(D)),Snapshot(MoveTemp(S)),Rules(MoveTemp(R)){}
FAetherObjectiveGuidance FAetherInteractionProvider::Guidance() const
{return AetherInteractionQueries::Guidance(Snapshot,Rules);}
TArray<FAetherInteractionOffer> FAetherInteractionProvider::Query(const FAetherInteractionQuery& Q) const
{
    TArray<FAetherInteractionOffer> Out;
    if(!ValidSnapshot(Snapshot)||Definition.Actions.Num()>16||Definition.Dialogue.Num()>64||!Rules.bValid||Q.ServerActorId.IsEmpty()||!Q.ServerActorId.Equals(Snapshot.CharacterId,ESearchCase::CaseSensitive)||!Q.TargetStableId.Equals(Snapshot.TargetStableId,ESearchCase::CaseSensitive)||
        !Snapshot.DefinitionId.Equals(Definition.Id,ESearchCase::CaseSensitive)||!Snapshot.bLoaded||Snapshot.ProfileRevision<0||Snapshot.WorldRevision<0||
        Snapshot.InteractionRevision<0||(!Snapshot.OwnerCharacterId.IsEmpty()&&!Snapshot.OwnerCharacterId.Equals(Q.ServerActorId,ESearchCase::CaseSensitive)))return Out;
    const auto Guide=Guidance();
    for(const auto& A:Definition.Actions)
    {
        if(HasAny(Snapshot.Claims,A.HideAfterClaims)||HasAny(Snapshot.Evidence,A.HideAfterEvidence))continue;
        const auto Quest=Missing(Snapshot.Claims,A.RequiredClaims),Fact=Missing(Snapshot.Evidence,A.RequiredEvidence);
        // 隐藏条件完全省略动作，不能把秘密任务 ID 或原因参数泄露给客户端。
        if(A.bHideLocked&&(!Quest.IsEmpty()||!Fact.IsEmpty()))continue;
        if(A.Kind==EAetherInteractionActionKind::TrackObjective&&!Guide.bHasTarget)continue;
        if(A.Kind==EAetherInteractionActionKind::LearnStorySkills&&!Snapshot.bHasStoryGrantAvailable)continue;
        if(A.Kind==EAetherInteractionActionKind::ClaimSkillPoints&&!Snapshot.bHasClaimableSkillPoints)continue;
        FAetherInteractionOffer O;O.TargetStableId=Snapshot.TargetStableId;O.TargetRevision=Snapshot.InteractionRevision;
        O.ProfileRevision=Snapshot.ProfileRevision;O.WorldRevision=Snapshot.WorldRevision;
        O.ActionId=A.Id;O.DisplayVerb=A.Verb;O.IconId=A.IconId;O.Priority=A.Priority;O.DialogueId=A.DialogueId;
        O.QuestId=A.QuestId;O.ObjectiveId=A.ObjectiveId;O.ServiceId=A.ServiceId;O.Availability=EAetherOfferAvailability::Available;
        const auto Disable=[&](const TCHAR* Reason){O.Availability=EAetherOfferAvailability::DisabledWithReason;O.ReasonId=Reason;};
        if(!Snapshot.bActorCanAct)Disable(TEXT("ActorNotReady"));
        else if(!Snapshot.bInRange||!Snapshot.bLineOfSight)Disable(TEXT("OutOfReach"));
        else if(Snapshot.bDowned)Disable(TEXT("TargetDowned"));
        else if(Snapshot.bThreatened)Disable(TEXT("TargetThreatened"));
        else if(Snapshot.bBusy)Disable(TEXT("TargetBusy"));
        else if(!Quest.IsEmpty()){Disable(TEXT("QuestRequired"));O.ReasonParameters.Add(TEXT("QuestId"),Quest);}
        else if(!Fact.IsEmpty()){Disable(TEXT("ObjectiveRequired"));O.ReasonParameters.Add(TEXT("ObjectiveId"),Fact);}
        else if(A.bSafeOnly&&Snapshot.bInCombat)Disable(TEXT("InCombat"));
        else if(!Snapshot.RegisteredHandlers.Contains(A.Kind))Disable(TEXT("ServiceUnavailable"));
        else if(A.Kind==EAetherInteractionActionKind::ClaimQuest)
        {
            const auto* Rule=Rules.Quest(FName(*A.QuestId));
            if(!Rule||!AvailableQuest(Snapshot,*Rule))Disable(TEXT("QuestUnavailable"));
            else for(const auto& Id:Rule->Objectives)if(!Snapshot.Evidence.Contains(Id.ToString())){Disable(TEXT("ObjectivesIncomplete"));break;}
        }
        if(A.Kind==EAetherInteractionActionKind::TrackObjective)
        {O.QuestId=Guide.QuestId;O.ObjectiveId=Guide.ObjectiveId;}
        if(A.Kind==EAetherInteractionActionKind::Talk&&O.Availability==EAetherOfferAvailability::Available)
            O.Availability=EAetherOfferAvailability::TalkOnly;
        Out.Add(MoveTemp(O));
    }
    Out.Sort([](const auto& A,const auto& B)
    {return A.Priority==B.Priority?A.ActionId.Compare(B.ActionId,ESearchCase::CaseSensitive)<0:A.Priority>B.Priority;});
    for(auto& O:Out)if(O.Availability==EAetherOfferAvailability::Available||O.Availability==EAetherOfferAvailability::TalkOnly)
    {O.bPreferred=true;break;}
    return Out;
}
EAetherCommandCode FAetherInteractionProvider::CheckSelection(const FAetherInteractionQuery& Q,const FAetherInteractionSelection& S) const
{
    using R=EAetherCommandCode;
    if(!ValidSnapshot(Snapshot))return R::NotReady;
    if(Q.ServerActorId.IsEmpty()||!Q.ServerActorId.Equals(Snapshot.CharacterId,ESearchCase::CaseSensitive)||
        (!Snapshot.OwnerCharacterId.IsEmpty()&&!Snapshot.OwnerCharacterId.Equals(Q.ServerActorId,ESearchCase::CaseSensitive)))return R::Unauthorized;
    if(!Q.TargetStableId.Equals(Snapshot.TargetStableId,ESearchCase::CaseSensitive)||!S.TargetStableId.Equals(Snapshot.TargetStableId,ESearchCase::CaseSensitive))return R::Missing;
    if(!Snapshot.bLoaded)return R::NotReady;
    if(S.ProfileRevision!=Snapshot.ProfileRevision||S.WorldRevision!=Snapshot.WorldRevision||
        S.InteractionRevision!=Snapshot.InteractionRevision)return R::StaleRevision;
    const auto Offers=Query(Q);
    const auto* O=Offers.FindByPredicate([&](const auto& V){return V.ActionId.Equals(S.ActionId,ESearchCase::CaseSensitive);});
    if(!O)return R::Missing;
    if(O->Availability==EAetherOfferAvailability::Available||O->Availability==EAetherOfferAvailability::TalkOnly)return R::Applied;
    if(O->ReasonId==TEXT("OutOfReach"))return R::OutOfReach;
    if(O->ReasonId==TEXT("ServiceUnavailable"))return R::UnsupportedAction;
    if(O->ReasonId==TEXT("TargetBusy"))return R::Busy;
    return R::NotReady;
}

TOptional<FAetherDialogueView> FAetherInteractionProvider::QueryDialogue(const FAetherInteractionQuery& Q,const FString& NodeId) const
{
    const auto Offers=Query(Q);TSet<FString> Reachable;
    for(const auto& O:Offers)if(O.Availability==EAetherOfferAvailability::TalkOnly&&!O.DialogueId.IsEmpty()&&Definition.Dialogue.Contains(O.DialogueId))
        Reachable.Add(O.DialogueId);
    for(int32 Pass=0;Pass<Definition.Dialogue.Num();++Pass)
    {
        const auto Before=Reachable.Num(),CurrentCount=Definition.Dialogue.Num();const auto Nodes=Reachable.Array();
        for(const auto& N:Nodes)for(const auto& Option:Definition.Dialogue[N].Options)
            if(!Option.NextNodeId.IsEmpty()&&Definition.Dialogue.Contains(Option.NextNodeId))Reachable.Add(Option.NextNodeId);
        if(Reachable.Num()==Before||Reachable.Num()==CurrentCount)break;
    }
    if(!Reachable.Contains(NodeId))return {};
    const auto& Node=Definition.Dialogue[NodeId];FAetherDialogueView View;View.NodeId=NodeId;View.Speaker=Node.Speaker;View.Text=Node.Text;
    for(const auto& O:Node.Options)
    {
        FAetherDialogueChoiceView Choice;Choice.Label=O.Label;Choice.ActionId=O.ActionId;Choice.NextNodeId=O.NextNodeId;
        if(!O.ActionId.IsEmpty())
        {
            const auto* Offer=Offers.FindByPredicate([&](const auto& A){return A.ActionId.Equals(O.ActionId,ESearchCase::CaseSensitive);});
            if(!Offer)continue; // 已完成/隐藏/无实际目标的选项不显示，也不残留秘密原因参数。
            Choice.Availability=Offer->Availability;Choice.ReasonId=Offer->ReasonId;Choice.ReasonParameters=Offer->ReasonParameters;
        }
        View.Choices.Add(MoveTemp(Choice));
    }
    return View;
}

EAetherCommandCode FAetherInteractionProvider::CheckCommand(const FString& Actor,const FAetherPlayerCommand& C) const
{
    if(C.Type!=EAetherCommandType::ExecuteInteraction)return EAetherCommandCode::UnsupportedAction;
    if(C.ProtocolVersion!=AetherCommands::LatestProtocolVersion)return EAetherCommandCode::UnsupportedProtocol;
    FString Reason;if(!AetherCommands::Validate(C,Reason))return EAetherCommandCode::Invalid;
    return CheckSelection({Actor,C.TargetStableId},{C.TargetStableId,C.ActionId,C.ExpectedProfileRevision,C.ExpectedWorldRevision,C.ExpectedInteractionRevision});
}
