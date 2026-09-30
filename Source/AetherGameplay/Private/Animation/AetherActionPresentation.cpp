#include "Animation/AetherActionPresentation.h"
#include "Combat/AetherCombat.h"
#include "Animation/AnimSequence.h"
void UAetherActionSet::PostLoad()
{
    Super::PostLoad();bDefinitionValid=DefinitionVersion==1&&FAetherControlledActionCatalog::Get().bValid&&Clips.Num()==AetherControlledActions::All().Num();
    USkeleton* Skeleton=nullptr;
    for(const auto& Expected:AetherControlledActions::All())
    {
        const auto* Entry=Clips.Find(Expected.ActionId);
        auto* Clip=Entry?Entry->Get():nullptr;
        if(!Expected.IsValid()||
            !Clip||!Clip->GetSkeleton()||!FMath::IsFinite(Clip->GetPlayLength())||!FMath::IsNearlyEqual(Clip->GetPlayLength(),Expected.Duration,.04f))
        {bDefinitionValid=false;UE_LOG(LogTemp,Error,TEXT("AETHER_ACTION_CONTENT_INVALID action=%s"),*Expected.ActionId.ToString());continue;}
        if(!Skeleton)Skeleton=Clip->GetSkeleton();else if(Skeleton!=Clip->GetSkeleton())bDefinitionValid=false;
    }
    if(!bDefinitionValid)UE_LOG(LogTemp,Error,TEXT("AETHER_ACTION_SET_REJECTED: version, timing, clip or skeleton mismatch"));
}
void AAetherCharacter::PresentAction(FName Id,float PlayRate)
{
    if(!HasAuthority()&&!IsLocallyControlled())return;
    const auto* Definition=AetherControlledActions::Find(Id);
    if(!Definition||!Definition->IsValid()||!FMath::IsFinite(PlayRate)||PlayRate<=0)return;
    const float Duration=Definition->Duration/PlayRate;
    if(!FMath::IsFinite(Duration)||Duration<=0)return;
    PresentedAction.bHasContact=false;PresentedAction.Contact=FVector::ZeroVector;
    PresentedAction.Id=Id;PresentedAction.StartedAt=CombatTime();PresentedAction.Duration=Duration;
    ++PresentedAction.Serial;if(HasAuthority())ForceNetUpdate();
}
