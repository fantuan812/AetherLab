#include "Animation/AetherActionPresentation.h"
#include "Combat/AetherCombat.h"
#include "Animation/AnimSequence.h"
UAetherActionSet::UAetherActionSet()
{
    for(const auto& D:AetherControlledActions::All())Rules.Add(D.ActionId,D);
}
void UAetherActionSet::PostLoad()
{
    Super::PostLoad();bDefinitionValid=DefinitionVersion==1;
    USkeleton* Skeleton=nullptr;
    for(const auto& Expected:AetherControlledActions::All())
    {
        const auto* Rule=Rules.Find(Expected.ActionId);const auto* Entry=Clips.Find(Expected.ActionId);
        auto* Clip=Entry?Entry->Get():nullptr;
        if(!Rule||!Rule->IsValid()||Rule->DefinitionVersion!=Expected.DefinitionVersion||
            Rule->Duration!=Expected.Duration||Rule->CommitTime!=Expected.CommitTime||Rule->Cost!=Expected.Cost||
            Rule->Cooldown!=Expected.Cooldown||Rule->MotionSpeed!=Expected.MotionSpeed||Rule->Impulse!=Expected.Impulse||
            !Clip||!Clip->GetSkeleton()||!FMath::IsFinite(Clip->GetPlayLength())||!FMath::IsNearlyEqual(Clip->GetPlayLength(),Expected.Duration,.04f))
        {bDefinitionValid=false;UE_LOG(LogTemp,Error,TEXT("AETHER_ACTION_CONTENT_INVALID action=%s"),*Expected.ActionId.ToString());continue;}
        if(!Skeleton)Skeleton=Clip->GetSkeleton();else if(Skeleton!=Clip->GetSkeleton())bDefinitionValid=false;
    }
    if(!bDefinitionValid)UE_LOG(LogTemp,Error,TEXT("AETHER_ACTION_SET_REJECTED: version, timing, clip or skeleton mismatch"));
}
void AAetherCharacter::PresentAction(FName Id,float Duration)
{
    if(!HasAuthority()&&!IsLocallyControlled())return;
    if(Id.IsNone()||!FMath::IsFinite(Duration)||Duration<=0||Duration>10)return;
    PresentedAction.bHasContact=false;PresentedAction.Contact=FVector::ZeroVector;
    PresentedAction.Id=Id;PresentedAction.StartedAt=CombatTime();PresentedAction.Duration=Duration;
    ++PresentedAction.Serial;if(HasAuthority())ForceNetUpdate();
}
