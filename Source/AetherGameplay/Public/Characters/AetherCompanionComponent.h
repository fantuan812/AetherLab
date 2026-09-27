#pragma once
#include "Components/ActorComponent.h"
#include "AetherCompanionComponent.generated.h"
class AAetherFrontierCharacter;
class AAetherCharacter;

namespace AetherRelations
{
    AETHERGAMEPLAY_API bool SameRecruitmentOwner(const AAetherFrontierCharacter& A,const AAetherFrontierCharacter& B);
    AETHERGAMEPLAY_API bool SameParty(const AAetherFrontierCharacter& A,const AAetherFrontierCharacter& B);
    AETHERGAMEPLAY_API bool Hostile(const AAetherCharacter& A,const AAetherCharacter& B);
    AETHERGAMEPLAY_API bool CanParticipateEncounter(const AAetherFrontierCharacter& A,const AAetherCharacter& Target);
    AETHERGAMEPLAY_API bool CanAssist(const AAetherFrontierCharacter& A,const AAetherFrontierCharacter& B);
}
UCLASS()
class AETHERGAMEPLAY_API UAetherCompanionComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UAetherCompanionComponent();
    virtual void TickComponent(float Delta,ELevelTick Type,FActorComponentTickFunction* Tick) override;
private:
    TWeakObjectPtr<AAetherCharacter> CombatTarget;
    TWeakObjectPtr<AAetherFrontierCharacter> RecruitmentOwner;
    float NextPerception=0;
};
