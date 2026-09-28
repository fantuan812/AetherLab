#include "Framework/AetherProgression.h"
#include "Definitions/AetherV10Definitions.h"
#include "Equipment/AetherEquipmentEffect.h"
#include "Inventory/AetherResourceGate.h"
#include "Combat/AetherDerivedStats.h"
#include "Effects/AetherBuffRuntime.h"

bool AAetherPlayerState::ApplyResolvedAttributes(const FAetherProfileStateV10& P,const TArray<FAetherExternalSkillGrant>& Grants,FString& Reason)
{
    auto* C=Cast<AAetherCharacter>(GetPawn());
    if(!HasAuthority()||!C||!AbilitySystem||AbilitySystem->GetAvatarActor()!=C)
    {Reason=TEXT("Attribute projection awaiting resource/life barrier");return false;}
    const auto& D=FAetherV10Definitions::Get();FAetherResolvedAttributes Candidate;
    if(!AetherAttributes::ResolveProfile(P,D.Items,D.Skills,Grants,C->BuffRuntime->GetState().Attributes(),Candidate,Reason))return false;
    // A read-only identical projection must not prevent consumable delivery releasing its gate.
    if(Candidate.Values.OrderIndependentCompareEqual(ResolvedAttributes.Values)&&
        C->MaxHealth==AetherDerivedStats::MaximumHealth(P.Experience,float(Candidate.Values.FindRef(TEXT("MaxHealth"))))&&
        NativeEquipmentSource.IsValid()&&AbilitySystem->GetActiveGameplayEffect(NativeEquipmentSource))
    {
        bool Changed=Candidate.Contributions.Num()!=ResolvedAttributes.Contributions.Num();
        if(!Changed)for(int32 I=0;I<Candidate.Contributions.Num();++I)
        {
            const auto& A=Candidate.Contributions[I];const auto& B=ResolvedAttributes.Contributions[I];
            Changed|=A.AttributeId!=B.AttributeId||A.SourceId!=B.SourceId||A.Value!=B.Value||A.Operation!=B.Operation||A.Priority!=B.Priority;
        }
        if(Changed){ResolvedAttributes=MoveTemp(Candidate);++ProjectionRevision;}
        return true;
    }
    if(C->ResourceGate->IsBlocked()&&!C->ResourceGate->IsEffectProjection()&&!(C->ResourceGate->IsRecovering()&&!C->ResourceGate->IsEnabled()))
    {Reason=TEXT("Attribute changes awaiting resource barrier");return false;}
    auto Gear=Candidate.Values;Gear.Remove(TEXT("MoveSpeed"));Gear.Remove(TEXT("ActionSpeed"));
    const float HP=C->Health(),MP=C->Mana(),SP=C->Stamina();
    if(!AetherEquipmentEffects::Publish(*AbilitySystem,NativeEquipmentSource,Gear,Reason))return false;
    if(!IsValid(C)||GetPawn()!=C||AbilitySystem->GetAvatarActor()!=C){Reason=TEXT("Avatar changed during attribute projection");return false;}
    C->MaxHealth=AetherDerivedStats::MaximumHealth(P.Experience,Attributes->GearMaxHealth.GetCurrentValue());
    if(C->ResourceGate->IsEffectProjection())
    {
        AbilitySystem->SetNumericAttributeBase(UAetherAttributes::GetHealthAttribute(),FMath::Min(HP,C->MaxHealth));
        AbilitySystem->SetNumericAttributeBase(UAetherAttributes::GetManaAttribute(),FMath::Min(MP,C->MaximumMana()));
        AbilitySystem->SetNumericAttributeBase(UAetherAttributes::GetStaminaAttribute(),FMath::Min(SP,C->MaximumStamina()));
    }
    else if(!(C->ResourceGate->IsRecovering()&&!C->ResourceGate->IsEnabled()))
        C->SetVitals(FMath::Min(HP,C->Health()),FMath::Min(MP,C->Mana()),FMath::Min(SP,C->Stamina()));
    ResolvedAttributes=MoveTemp(Candidate);++ProjectionRevision;return true;
}
