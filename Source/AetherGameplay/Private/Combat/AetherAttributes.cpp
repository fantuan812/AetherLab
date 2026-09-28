#include "Combat/AetherAttributes.h"
#include "Net/UnrealNetwork.h"
#include "Combat/AetherCombat.h"
#include "GameplayEffectExtension.h"
void UAetherAttributes::ClampValue(const FGameplayAttribute& Attribute,float& Value) const
{
    if(!FMath::IsFinite(Value))Value=0;
    float Max=100000;
    const auto* ASC=GetOwningAbilitySystemComponent();
    const auto* C=ASC?Cast<AAetherCharacter>(ASC->GetAvatarActor()):nullptr;
    if(Attribute==GetHealthAttribute())Max=C?C->MaxHealth:100;
    else if(Attribute==GetManaAttribute())Max=C?C->MaximumMana():100;
    else if(Attribute==GetStaminaAttribute())Max=C?C->MaximumStamina():100;
    else if(Attribute==GetPostureAttribute())Max=100;
    Value=FMath::Clamp(Value,0.f,FMath::IsFinite(Max)?FMath::Max(1.f,Max):100.f);
}
void UAetherAttributes::PreAttributeChange(const FGameplayAttribute& Attribute,float& Value)
{Super::PreAttributeChange(Attribute,Value);ClampValue(Attribute,Value);}
void UAetherAttributes::PreAttributeBaseChange(const FGameplayAttribute& Attribute,float& Value) const
{Super::PreAttributeBaseChange(Attribute,Value);ClampValue(Attribute,Value);}
void UAetherAttributes::PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data)
{
    Super::PostGameplayEffectExecute(Data);
    const auto A=Data.EvaluatedData.Attribute;auto* ASC=GetOwningAbilitySystemComponent();if(!ASC)return;
    const float Current=ASC->GetNumericAttribute(A);float Value=Current;ClampValue(A,Value);
    if(!FMath::IsFinite(Current)||Current!=Value)ASC->SetNumericAttributeBase(A,Value);
}
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
