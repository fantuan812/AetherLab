#pragma once
#include "GameplayEffect.h"
#include "Inventory/AetherInventoryState.h"
#include "AetherEquipmentEffect.generated.h"

class UAbilitySystemComponent;

// 唯一装备来源效果：值来自已提交库存的统一计算器，不把每次发布做成永久加法。
UCLASS()
class AETHERLAB_API UAetherEquipmentEffect : public UGameplayEffect
{
    GENERATED_BODY()
public:
    UAetherEquipmentEffect();
};
namespace AetherEquipmentEffects
{
    AETHERLAB_API bool Publish(UAbilitySystemComponent& ASC,FActiveGameplayEffectHandle& Source,
        const TMap<FString,double>& Stats,FString& Reason);
}
