#include "Animation/AetherActionPresentation.h"
#include "Combat/AetherCombat.h"
#include "Animation/AnimSequence.h"
UAetherActionSet::UAetherActionSet()
{
    for(const auto& D:AetherControlledActions::All())Rules.Add(D.ActionId,D);
}
void UAetherActionSet::PostLoad()
{
    Super::PostLoad();bDefinitionValid=DefinitionVersion==1&&Rules.Num()==AetherControlledActions::All().Num();
    USkeleton* Skeleton=nullptr;
    for(const auto& Expected:AetherControlledActions::All())
    {
        const auto* Rule=Rules.Find(Expected.ActionId);const auto* Entry=Clips.Find(Expected.ActionId);
        auto* Clip=Entry?Entry->Get():nullptr;
        if(!Rule||!Rule->IsValid()||Rule->DefinitionVersion!=Expected.DefinitionVersion||
            Rule->Duration!=Expected.Duration||Rule->CommitTime!=Expected.CommitTime||Rule->Cost!=Expected.Cost||
            Rule->RecoveryTime!=Expected.RecoveryTime||Rule->Cooldown!=Expected.Cooldown||
            Rule->MotionSpeed!=Expected.MotionSpeed||Rule->MotionTime!=Expected.MotionTime||
            Rule->InvulnerabilityTime!=Expected.InvulnerabilityTime||Rule->Impulse!=Expected.Impulse||
            Rule->bLoop!=Expected.bLoop||Rule->AllowedStances!=Expected.AllowedStances||
            Rule->ContactPolicy!=Expected.ContactPolicy||Rule->CancelPolicy!=Expected.CancelPolicy||
            !Clip||!Clip->GetSkeleton()||!FMath::IsFinite(Clip->GetPlayLength())||!FMath::IsNearlyEqual(Clip->GetPlayLength(),Expected.Duration,.04f))
        {bDefinitionValid=false;UE_LOG(LogTemp,Error,TEXT("AETHER_ACTION_CONTENT_INVALID action=%s"),*Expected.ActionId.ToString());continue;}
        if(!Skeleton)Skeleton=Clip->GetSkeleton();else if(Skeleton!=Clip->GetSkeleton())bDefinitionValid=false;
    }
    if(!bDefinitionValid)UE_LOG(LogTemp,Error,TEXT("AETHER_ACTION_SET_REJECTED: version, timing, clip or skeleton mismatch"));
}
void AAetherCharacter::PresentAction(FName Id,float Duration)
{
    if(!HasAuthority()&&!IsLocallyControlled())return;
    // 已登记动作的播放锁由共同规则决定；调用方参数仅供未登记的临时表现使用。
    if(const auto* Definition=AetherControlledActions::Find(Id))Duration=Definition->Duration;
    if(Id.IsNone()||!FMath::IsFinite(Duration)||Duration<=0||Duration>10)return;
    PresentedAction.bHasContact=false;PresentedAction.Contact=FVector::ZeroVector;
    PresentedAction.Id=Id;PresentedAction.StartedAt=CombatTime();PresentedAction.Duration=Duration;
    ++PresentedAction.Serial;if(HasAuthority())ForceNetUpdate();
}
