#pragma once
#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "AbilitySystemComponent.h"
#include "AetherAttributes.generated.h"
UCLASS()
class AETHERGAMEPLAY_API UAetherAttributes : public UAttributeSet
{
    GENERATED_BODY()
public:
    UAetherAttributes();
    UPROPERTY(ReplicatedUsing=OnRep_Health) FGameplayAttributeData Health;
    UPROPERTY(ReplicatedUsing=OnRep_Mana) FGameplayAttributeData Mana;
    UPROPERTY(ReplicatedUsing=OnRep_Stamina) FGameplayAttributeData Stamina;
    UPROPERTY(ReplicatedUsing=OnRep_Posture) FGameplayAttributeData Posture;
    UPROPERTY(ReplicatedUsing=OnRep_GearDamage) FGameplayAttributeData GearDamage;
    GAMEPLAYATTRIBUTE_PROPERTY_GETTER(UAetherAttributes, GearDamage)
    UPROPERTY(ReplicatedUsing=OnRep_GearPosture) FGameplayAttributeData GearPosture;
    GAMEPLAYATTRIBUTE_PROPERTY_GETTER(UAetherAttributes, GearPosture)
    UPROPERTY(ReplicatedUsing=OnRep_GearArmor) FGameplayAttributeData GearArmor;
    GAMEPLAYATTRIBUTE_PROPERTY_GETTER(UAetherAttributes, GearArmor)
    UPROPERTY(ReplicatedUsing=OnRep_GearFireResist) FGameplayAttributeData GearFireResist;
    GAMEPLAYATTRIBUTE_PROPERTY_GETTER(UAetherAttributes, GearFireResist)
    UPROPERTY(ReplicatedUsing=OnRep_GearWaterResist) FGameplayAttributeData GearWaterResist;
    GAMEPLAYATTRIBUTE_PROPERTY_GETTER(UAetherAttributes, GearWaterResist)
    UPROPERTY(ReplicatedUsing=OnRep_GearFrostResist) FGameplayAttributeData GearFrostResist;
    GAMEPLAYATTRIBUTE_PROPERTY_GETTER(UAetherAttributes, GearFrostResist)
    UPROPERTY(ReplicatedUsing=OnRep_GearStormResist) FGameplayAttributeData GearStormResist;
    GAMEPLAYATTRIBUTE_PROPERTY_GETTER(UAetherAttributes, GearStormResist)
    UPROPERTY(ReplicatedUsing=OnRep_GearMaxHealth) FGameplayAttributeData GearMaxHealth;
    GAMEPLAYATTRIBUTE_PROPERTY_GETTER(UAetherAttributes, GearMaxHealth)
    UPROPERTY(ReplicatedUsing=OnRep_GearMaxMana) FGameplayAttributeData GearMaxMana;
    GAMEPLAYATTRIBUTE_PROPERTY_GETTER(UAetherAttributes, GearMaxMana)
    UPROPERTY(ReplicatedUsing=OnRep_GearMaxStamina) FGameplayAttributeData GearMaxStamina;
    GAMEPLAYATTRIBUTE_PROPERTY_GETTER(UAetherAttributes, GearMaxStamina)
    GAMEPLAYATTRIBUTE_PROPERTY_GETTER(UAetherAttributes, Health)
    GAMEPLAYATTRIBUTE_PROPERTY_GETTER(UAetherAttributes, Mana)
    GAMEPLAYATTRIBUTE_PROPERTY_GETTER(UAetherAttributes, Stamina)
    GAMEPLAYATTRIBUTE_PROPERTY_GETTER(UAetherAttributes, Posture)
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
private:
    UFUNCTION() void OnRep_GearDamage(const FGameplayAttributeData& Old);
    UFUNCTION() void OnRep_GearPosture(const FGameplayAttributeData& Old);
    UFUNCTION() void OnRep_GearArmor(const FGameplayAttributeData& Old);
    UFUNCTION() void OnRep_GearFireResist(const FGameplayAttributeData& Old);
    UFUNCTION() void OnRep_GearWaterResist(const FGameplayAttributeData& Old);
    UFUNCTION() void OnRep_GearFrostResist(const FGameplayAttributeData& Old);
    UFUNCTION() void OnRep_GearStormResist(const FGameplayAttributeData& Old);
    UFUNCTION() void OnRep_GearMaxHealth(const FGameplayAttributeData& Old);
    UFUNCTION() void OnRep_GearMaxMana(const FGameplayAttributeData& Old);
    UFUNCTION() void OnRep_GearMaxStamina(const FGameplayAttributeData& Old);
    UFUNCTION() void OnRep_Health(const FGameplayAttributeData& Old);
    UFUNCTION() void OnRep_Mana(const FGameplayAttributeData& Old);
    UFUNCTION() void OnRep_Stamina(const FGameplayAttributeData& Old);
    UFUNCTION() void OnRep_Posture(const FGameplayAttributeData& Old);
};
