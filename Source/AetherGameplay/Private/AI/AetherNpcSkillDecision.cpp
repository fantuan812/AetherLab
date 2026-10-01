#include "AI/AetherNpcSkillDecision.h"
#include "Combat/AetherCombat.h"
#include "Characters/AetherFrontierCharacter.h"
#include "Characters/AetherCompanionComponent.h"
#include "Skills/AetherNpcSkillDefinitions.h"
#include "Skills/AetherSkillAbilityBinding.h"
#include "AIController.h"

namespace
{
bool CanActivateSkill(const AAetherCharacter& Character,const FString& Id)
{
    const auto* System=Character.AbilitySystem.Get();
    const auto* Spec=System?AetherSkillBinding::Find(*System,Id):nullptr;
    return Spec&&Spec->Ability&&!Spec->IsActive()&&Character.SkillUnlocked(Id)&&System->AbilityActorInfo.IsValid()&&
        Spec->Ability->CanActivateAbility(Spec->Handle,System->AbilityActorInfo.Get(),nullptr,nullptr,nullptr);
}
bool HitsTarget(const AAetherCharacter& Character,const AAetherCharacter& Target,const FString& Id,int32 Rank)
{
    FHitResult Hit;FVector Origin,Direction;
    return Character.FindSkillTarget(Id,Rank,Hit,Origin,Direction)&&Hit.GetActor()==&Target;
}
}
bool FAetherNpcSkillDecision::IsControlled(const AAetherCharacter& C)
{
    return C.HasAuthority()&&C.Alive()&&C.Fighter!=EAetherFighter::Player&&C.SkillAuthority==EAetherSkillAuthority::Definition&&
        !C.GetPlayerState<AAetherPlayerState>()&&Cast<AAIController>(C.GetController())&&C.AbilitySystem&&
        C.AbilitySystem->GetOwnerActor()==&C&&C.AbilitySystem->GetAvatarActor()==&C;
}
bool FAetherNpcSkillDecision::CanTarget(const AAetherCharacter& C,const AAetherCharacter& Target)
{
    if(!IsControlled(C)||!IsValid(&Target)||Target.IsActorBeingDestroyed()||!Target.Alive()||C.GetWorld()!=Target.GetWorld()||!AetherRelations::Hostile(C,Target))return false;
    if(Cast<AAetherCharacter>(C.GetOwner())&&C.GetOwner()!=&Target)return false;
    if(const auto* Enemy=Cast<AAetherFrontierCharacter>(&C);Enemy&&!Enemy->EncounterId.IsNone())
    {
        const auto* Participant=Cast<AAetherFrontierCharacter>(&Target);
        return Participant&&AetherRelations::CanParticipateEncounter(*Participant,C);
    }
    return true;
}
bool FAetherNpcSkillDecision::SameControl(const AAetherCharacter& C) const
{return IsControlled(C)&&OwnedSystem.Get()==C.AbilitySystem&&OwnedController.Get()==C.GetController();}
bool FAetherNpcSkillDecision::SynchronizeControl(const AAetherCharacter& C)
{
    if(!IsControlled(C)){Reset();return false;}
    if(!SameControl(C)){Reset();OwnedSystem=C.AbilitySystem;OwnedController=C.GetController();}
    if(!bIssuingRequest&&!RequestedSkillId.IsEmpty()&&C.CastExecutionId!=RequestedExecution)CancelRequest();
    return true;
}
void FAetherNpcSkillDecision::CancelRequest()
{RequestedSkillId.Reset();RequestedTarget.Reset();RequestedExecution.Invalidate();bIssuingRequest=false;}
void FAetherNpcSkillDecision::Reset()
{CancelRequest();LastCommittedSkillId.Reset();OwnedSystem.Reset();OwnedController.Reset();}
FAetherNpcSkillChoice FAetherNpcSkillDecision::Choose(AAetherCharacter& C,AAetherCharacter& Target)
{
    FAetherNpcSkillChoice Choice;
    if(!SynchronizeControl(C)||!CanTarget(C,Target))return Choice;
    const auto* Loadout=FAetherNpcSkillDefinitions::Get().Find(C.SkillLoadoutId);
    if(!Loadout)return Choice;
    if(Loadout->OffensiveSkills.IsEmpty()){Choice.Kind=EAetherNpcSkillChoice::NoOffensiveSkills;return Choice;}
    const auto& Definitions=FAetherSkillDefinitionsV10::Get();
    // 资料要求的能力缺失是装配不可用，不用其他技能或近战掩盖缺口。
    for(const auto& Id:Loadout->OffensiveSkills)
    {
        const auto* Spec=AetherSkillBinding::Find(*C.AbilitySystem,Id);
        const auto* Definition=Definitions.Skills.Find(Id);
        if(!Spec||!C.SkillUnlocked(Id)||!Definitions.Effect(Id,Spec->Level)||!Definition||!FAetherNpcSkillDefinitions::SupportsOffensiveActorTarget(*Definition))return Choice;
    }
    Choice.Kind=EAetherNpcSkillChoice::Waiting;
    if(bIssuingRequest||!RequestedSkillId.IsEmpty()||C.QueryAction(EAetherActionKind::Spell)!=EAetherActionDenial::None)return Choice;
    const int32 Previous=Loadout->OffensiveSkills.IndexOfByKey(LastCommittedSkillId);
    for(int32 Offset=1;Offset<=Loadout->OffensiveSkills.Num();++Offset)
    {
        const FString& Id=Loadout->OffensiveSkills[(Previous+Offset)%Loadout->OffensiveSkills.Num()];
        if(!CanActivateSkill(C,Id))continue;
        const auto* Spec=AetherSkillBinding::Find(*C.AbilitySystem,Id);
        const auto* Effect=Definitions.Effect(Id,Spec->Level);
        if(HitsTarget(C,Target,Id,Spec->Level))return {EAetherNpcSkillChoice::Ready,Id,Effect->RangeCm};
        if(Effect->RangeCm>Choice.RangeCm)Choice={EAetherNpcSkillChoice::Approach,Id,Effect->RangeCm};
    }
    return Choice;
}
bool FAetherNpcSkillDecision::TryExecute(AAetherCharacter& C,AAetherCharacter& Target,const FString& Id)
{
    if(!SynchronizeControl(C)||!RequestedSkillId.IsEmpty()||!CanTarget(C,Target))return false;
    const auto* Loadout=FAetherNpcSkillDefinitions::Get().Find(C.SkillLoadoutId);
    const auto* Spec=C.AbilitySystem?AetherSkillBinding::Find(*C.AbilitySystem,Id):nullptr;
    if(!Loadout||!Loadout->OffensiveSkills.Contains(Id)||!Spec||!CanActivateSkill(C,Id)||!HitsTarget(C,Target,Id,Spec->Level))return false;
    RequestedSkillId=Id;RequestedTarget=&Target;bIssuingRequest=true;
    const bool Activated=C.TrySkill(Id);bIssuingRequest=false;
    // 零前摇可能已同步完成；有前摇则锁住 GAS 的真实执行身份。
    if(!RequestedSkillId.IsEmpty())
    {
        if(Activated&&C.CastExecutionId.IsValid())RequestedExecution=C.CastExecutionId;
        else CancelRequest();
    }
    return Activated;
}
bool FAetherNpcSkillDecision::ValidateCommit(AAetherCharacter& C,const FAetherCastExecution& Cast)
{
    if(RequestedSkillId.IsEmpty())return true; // 普通玩家/训练探针仍使用其原有 GAS 目标合同。
    if(bIssuingRequest&&!RequestedExecution.IsValid())RequestedExecution=Cast.ExecutionId;
    const auto* Loadout=FAetherNpcSkillDefinitions::Get().Find(C.SkillLoadoutId);
    const auto* Target=RequestedTarget.Get();
    return SameControl(C)&&Target&&RequestedSkillId==Cast.SkillId&&RequestedExecution==Cast.ExecutionId&&
        C.CastExecutionId==RequestedExecution&&Loadout&&Loadout->OffensiveSkills.Contains(Cast.SkillId)&&
        CanTarget(C,*Target)&&HitsTarget(C,*Target,Cast.SkillId,Cast.Rank);
}
void FAetherNpcSkillDecision::RecordCommitted(const AAetherCharacter& C,const FAetherCastExecution& Cast)
{
    if(SameControl(C)&&RequestedSkillId==Cast.SkillId&&RequestedExecution==Cast.ExecutionId)
    {LastCommittedSkillId=Cast.SkillId;CancelRequest();}
}
