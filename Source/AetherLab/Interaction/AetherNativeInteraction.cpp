#include "Interaction/AetherNativeInteraction.h"
#include "Characters/AetherFrontierCharacter.h"
#include "World/AetherFrontierProp.h"
#include "World/AetherFrontierState.h"
#include "Networking/AetherCommandClient.h"
#include "Interaction/AetherNearbyRegistry.h"
#include "Definitions/AetherV10Definitions.h"
#include "Contracts/AetherTransaction.h"
#include "Engine/LocalPlayer.h"
#include "EngineUtils.h"

bool AetherNativeInteraction::IsPersistent(EAetherInteractionActionKind K)
{
    using E=EAetherInteractionActionKind;
    switch(K)
    {
    case E::Register:case E::BindInn:case E::LearnStorySkills:case E::ClaimQuest:case E::ClaimSkillPoints:
    case E::CollectSupply:case E::CollectGather:case E::ClaimDaily:case E::ObserveObjective:case E::RecordDaily:case E::RestoreWaterService:case E::SetPower:return true;
    default:return false;
    }
}
TOptional<FAetherInteractionProvider> AetherNativeInteraction::Provider(AAetherFrontierCharacter& C,AAetherFrontierProp& Target)
{
    const auto* PS=C.ProfileState();auto* PC=Cast<APlayerController>(C.GetController());
    auto* Client=PC&&PC->GetLocalPlayer()?PC->GetLocalPlayer()->GetSubsystem<UAetherCommandClient>():nullptr;
    const auto* P=C.HasAuthority()&&PS?PS->GetNativeProfile():Client&&Client->GetProfile().IsSet()?&Client->GetProfile().GetValue():nullptr;
    const auto* State=C.GetWorld()->GetGameState<AAetherFrontierState>();const auto& D=FAetherV10Definitions::Get();
    const auto* Definition=D.Interactions.Targets.Find(Target.Service.ToString());
    const auto* Registry=C.GetWorld()->GetSubsystem<UAetherNearbyRegistry>();
    if(!P||!PS||!D.bValid||!Definition||!State||State->NativeWorldRevision<0||!Registry||!Registry->Contains(&Target)||
        !P->CharacterId.Equals(PS->Profile.CharacterId,ESearchCase::CaseSensitive))return {};
    FAetherInteractionSnapshot S;S.CharacterId=P->CharacterId;S.TargetStableId=Target.Spec.Id.ToString();S.DefinitionId=Definition->Id;
    S.ProfileRevision=P->Revision;S.WorldRevision=State->NativeWorldRevision;S.InteractionRevision=Target.InteractionRevision;
    S.bLoaded=Target.bEnabled&&!Target.IsActorBeingDestroyed();S.bActorCanAct=C.Ready()&&!C.bTravelPending;
    S.bInRange=FVector::DistSquared(C.GetActorLocation(),Target.GetActorLocation())<=FMath::Square(250.);
    FCollisionQueryParams Q(SCENE_QUERY_STAT(NativeOffer),false,&C);Q.AddIgnoredActor(&Target);
    S.bLineOfSight=!C.GetWorld()->LineTraceTestByChannel(C.GetActorLocation(),Target.GetActorLocation(),ECC_Visibility,Q);
    S.bBusy=C.Carried||C.ReviveTarget;S.bDowned=!C.Alive();S.bInCombat=C.HasRecentCombat(8);
    S.bPowerEnabled=Target.Mechanism&&Target.Mechanism->bPowerEnabled;S.bServiceComplete=Target.bWorkshopService?State->bWorkshopRestored:State->bSupplyRestored;
    if(Target.Reactive->bOwnerOnlyStimuli)
    {
        // 私人目标即使网络上暂时可见，也不能查询另一个角色的任务原因。
        if(Target.GetOwner()!=&C)return {};
        S.OwnerCharacterId=P->CharacterId;
    }
    for(const auto& Id:P->Claims)S.Claims.Add(Id);for(const auto& Id:P->Evidence)S.Evidence.Add(Id);
    S.bHasClaimableSkillPoints=AetherQuestProgression::HasClaimableSkillPoints(*P,D.Progression);
    for(const auto& Skill:D.Skills.Skills)
        S.bHasStoryGrantAvailable|=Skill.Value.bStoryBase&&!P->Skills.StoryGrants.Contains(Skill.Key)&&
            (Skill.Value.RequiredQuest.IsEmpty()||P->Claims.Contains(Skill.Value.RequiredQuest));
    for(const auto& Action:Definition->Actions)if(IsPersistent(Action.Kind))S.RegisteredHandlers.Add(Action.Kind);
    return FAetherInteractionProvider(*Definition,MoveTemp(S),D.Rules);
}
bool AetherNativeInteraction::Submit(AAetherFrontierCharacter& C,const FAetherInteractionSelection& S,FString& Reason)
{
    auto* PC=Cast<APlayerController>(C.GetController());auto* LP=PC?PC->GetLocalPlayer():nullptr;
    auto* Client=LP?LP->GetSubsystem<UAetherCommandClient>():nullptr;
    if(!Client||!Client->GetProfile().IsSet()||!Client->GetChannel().IsValid()||Client->HasPending()||
        Client->GetProfile()->Revision!=S.ProfileRevision){Reason=TEXT("角色快照已变化或已有请求等待确认。");return false;}
    FAetherPlayerCommand Command;Command.ProtocolVersion=AetherCommands::LatestProtocolVersion;
    Command.CommandId=AetherTransactions::NewCommandId(S.ProfileRevision);Command.Type=EAetherCommandType::ExecuteInteraction;
    Command.ExpectedProfileRevision=S.ProfileRevision;Command.ExpectedWorldRevision=S.WorldRevision;Command.ExpectedInteractionRevision=S.InteractionRevision;
    Command.TargetStableId=S.TargetStableId;Command.ActionId=S.ActionId;
    TArray<uint8> Bytes;
    if(!AetherCommands::Encode(Command,Bytes,Reason))return false;
    return Client->Submit(Client->GetChannel(),Client->GetOwnerIdentity(),Bytes,Reason);
}
