#include "Combat/AetherAttributes.h"
#include "Net/UnrealNetwork.h"
UAetherAttributes::UAetherAttributes()
{ Health.SetBaseValue(100); Health.SetCurrentValue(100); Mana.SetBaseValue(100); Mana.SetCurrentValue(100);
  Stamina.SetBaseValue(100); Stamina.SetCurrentValue(100); Posture.SetBaseValue(0); Posture.SetCurrentValue(0); }
void UAetherAttributes::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME_CONDITION_NOTIFY(UAetherAttributes, GearDamage, COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UAetherAttributes, GearPosture, COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UAetherAttributes, GearArmor, COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UAetherAttributes, GearFireResist, COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UAetherAttributes, GearWaterResist, COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UAetherAttributes, GearFrostResist, COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UAetherAttributes, GearStormResist, COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UAetherAttributes, GearMaxHealth, COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UAetherAttributes, GearMaxMana, COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UAetherAttributes, GearMaxStamina, COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UAetherAttributes, Health, COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UAetherAttributes, Mana, COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UAetherAttributes, Stamina, COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UAetherAttributes, Posture, COND_None, REPNOTIFY_Always);
}
void UAetherAttributes::OnRep_Health(const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(UAetherAttributes, Health, Old); }
void UAetherAttributes::OnRep_Mana(const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(UAetherAttributes, Mana, Old); }
void UAetherAttributes::OnRep_Stamina(const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(UAetherAttributes, Stamina, Old); }
void UAetherAttributes::OnRep_Posture(const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(UAetherAttributes, Posture, Old); }

void UAetherAttributes::OnRep_GearDamage(const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(UAetherAttributes, GearDamage, Old); }
void UAetherAttributes::OnRep_GearPosture(const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(UAetherAttributes, GearPosture, Old); }
void UAetherAttributes::OnRep_GearArmor(const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(UAetherAttributes, GearArmor, Old); }
void UAetherAttributes::OnRep_GearFireResist(const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(UAetherAttributes, GearFireResist, Old); }
void UAetherAttributes::OnRep_GearWaterResist(const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(UAetherAttributes, GearWaterResist, Old); }
void UAetherAttributes::OnRep_GearFrostResist(const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(UAetherAttributes, GearFrostResist, Old); }
void UAetherAttributes::OnRep_GearStormResist(const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(UAetherAttributes, GearStormResist, Old); }
void UAetherAttributes::OnRep_GearMaxHealth(const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(UAetherAttributes, GearMaxHealth, Old); }
void UAetherAttributes::OnRep_GearMaxMana(const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(UAetherAttributes, GearMaxMana, Old); }
void UAetherAttributes::OnRep_GearMaxStamina(const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(UAetherAttributes, GearMaxStamina, Old); }
