#include "AetherProgression.h"
#include "Net/UnrealNetwork.h"
AAetherPlayerState::AAetherPlayerState()
{
    AbilitySystem=CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("PersistentAbilities")); AbilitySystem->SetIsReplicated(true);
    AbilitySystem->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);
    Attributes=CreateDefaultSubobject<UAetherAttributes>(TEXT("PersistentAttributes")); Attributes->Posture.SetBaseValue(100); Attributes->Posture.SetCurrentValue(100); SetNetUpdateFrequency(20);
}
void AAetherPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{ Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME_CONDITION(AAetherPlayerState,Profile,COND_OwnerOnly);DOREPLIFETIME(AAetherPlayerState,DisplayName);DOREPLIFETIME(AAetherPlayerState,PartyLeader);DOREPLIFETIME_CONDITION(AAetherPlayerState,InvitedBy,COND_OwnerOnly); }
