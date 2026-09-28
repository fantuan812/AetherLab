#include "Framework/AetherProgression.h"
#include "Definitions/AetherV10Definitions.h"
#include "Equipment/AetherEquipmentEffect.h"
#include "Inventory/AetherResourceGate.h"
#include "Combat/AetherDerivedStats.h"

bool AAetherPlayerState::ApplyResolvedAttributes(const FAetherProfileStateV10& P,const TArray<FAetherExternalSkillGrant>& Grants,FString& Reason)
{
    auto* C=Cast<AAetherCharacter>(GetPawn());
    if(!HasAuthority()||!C||!AbilitySystem||AbilitySystem->GetAvatarActor()!=C)
    {Reason=TEXT("Attribute projection awaiting resource/life barrier");return false;}
    const auto& D=FAetherV10Definitions::Get();FAetherResolvedAttributes Candidate;
    if(!AetherAttributes::ResolveProfile(P,D.Items,D.Skills,Grants,{},Candidate,Reason))return false;
    // A read-only identical projection must not prevent consumable delivery releasing its gate.
    if(Candidate.Values.OrderIndependentCompareEqual(ResolvedAttributes.Values)&&
        NativeEquipmentSource.IsValid()&&AbilitySystem->GetActiveGameplayEffect(NativeEquipmentSource))return true;
    if(C->ResourceGate->IsBlocked()&&!(C->ResourceGate->IsRecovering()&&!C->ResourceGate->IsEnabled()))
    {Reason=TEXT("Attribute changes awaiting resource barrier");return false;}
    auto Gear=Candidate.Values;Gear.Remove(TEXT("MoveSpeed"));Gear.Remove(TEXT("ActionSpeed"));
    const float HP=C->Health(),MP=C->Mana(),SP=C->Stamina();
    if(!AetherEquipmentEffects::Publish(*AbilitySystem,NativeEquipmentSource,Gear,Reason))return false;
    if(!IsValid(C)||GetPawn()!=C||AbilitySystem->GetAvatarActor()!=C){Reason=TEXT("Avatar changed during attribute projection");return false;}
    C->MaxHealth=AetherDerivedStats::MaximumHealth(P.Experience,Attributes->GearMaxHealth.GetCurrentValue());
    if(!(C->ResourceGate->IsRecovering()&&!C->ResourceGate->IsEnabled()))
        C->SetVitals(FMath::Min(HP,C->Health()),FMath::Min(MP,C->Mana()),FMath::Min(SP,C->Stamina()));
    ResolvedAttributes=MoveTemp(Candidate);++ProjectionRevision;return true;
}
