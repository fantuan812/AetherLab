#pragma once
#include "Components/ActorComponent.h"
#include "AetherCompanionComponent.generated.h"
class AAetherFrontierCharacter;
class AAetherCharacter;
class AController;

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
    bool BeginCompanionControl();
    void EndCompanionControl();
    virtual void TickComponent(float Delta,ELevelTick Type,FActorComponentTickFunction* Tick) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    void ClearOwnedActions();
    TWeakObjectPtr<AAetherCharacter> CombatTarget;
    TWeakObjectPtr<AAetherFrontierCharacter> RecruitmentOwner;
    TWeakObjectPtr<AAetherFrontierCharacter> OwnedReviveTarget;
    TWeakObjectPtr<AController> ManagedController;
    uint64 OwnedHealRequest=0,ControlGeneration=0;
    bool bManaged=false,bOwnsGuard=false;
    float NextPerception=0;
};
