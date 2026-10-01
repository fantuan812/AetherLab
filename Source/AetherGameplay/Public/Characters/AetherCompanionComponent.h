#pragma once
#include "Components/ActorComponent.h"
#include "AI/AetherCompanionSupportDefinitions.h"
#include "GameplayAbilitySpec.h"
#include "AetherCompanionComponent.generated.h"
class AAetherFrontierCharacter;
class AAetherCharacter;
class AController;
class UAbilitySystemComponent;
struct FAetherCastExecution;

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
    bool ValidateSkillCommit(const FAetherCastExecution& Cast,const AAetherCharacter* ActualTarget);
    virtual void TickComponent(float Delta,ELevelTick Type,FActorComponentTickFunction* Tick) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    friend class FAetherCompanionHealTest;
    void ClearOwnedActions();
    void CancelOwnedSupport();
    bool HasSupportControl(const AAetherFrontierCharacter& Character) const;
    bool ValidPatient(const AAetherFrontierCharacter& Character,const AAetherFrontierCharacter& Patient,const FAetherCompanionSupportProfile& Policy) const;
    const FAetherCompanionSupportProfile* SupportPolicy(AAetherFrontierCharacter& Character);
    bool TrySupport(AAetherFrontierCharacter& Character);
    bool TryIssueSupport(AAetherFrontierCharacter& Character,AAetherFrontierCharacter& Patient,const FAetherCompanionSupportProfile& Policy,EAetherCompanionSupportPurpose Purpose);
    void SupportUnavailable(AAetherFrontierCharacter& Character,const FString& Reason);
    struct FSupportIntent
    {
        FGuid RequestId=FGuid::NewGuid(),Execution,Life,TargetLife;
        FString SkillId,Loadout;
        int32 Rank=0;
        EAetherCompanionSupportPurpose Purpose=EAetherCompanionSupportPurpose::SelfHealing;
        FGameplayAbilitySpecHandle Spec;
        TWeakObjectPtr<UAbilitySystemComponent> System,TargetSystem;
        TWeakObjectPtr<AAetherFrontierCharacter> Patient,Recruiter;
        TWeakObjectPtr<AController> Controller;
        uint64 Generation=0,DamageSerial=0;
        bool bIssuing=false,bCanceled=false;
    };
    bool IsSupportIntentValid(const FSupportIntent& Intent) const;
    TSharedPtr<FSupportIntent> SupportIntent;
    FGuid CanceledSupportExecution;
    double NextSupportSample=0;
    FString SupportDiagnostic;
    TWeakObjectPtr<AAetherCharacter> CombatTarget;
    TWeakObjectPtr<AAetherFrontierCharacter> RecruitmentOwner;
    TWeakObjectPtr<AAetherFrontierCharacter> OwnedReviveTarget;
    TWeakObjectPtr<AController> ManagedController;
    uint64 ControlGeneration=0;
    bool bManaged=false,bOwnsGuard=false;
    float NextPerception=0;
};

