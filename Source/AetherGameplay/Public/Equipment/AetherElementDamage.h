#pragma once
#include "GameFramework/DamageType.h"
#include "AetherElementDamage.generated.h"

// 区分角色伤害结算渠道；仅改变生命损失，不抹掉已经守恒注入世界的水、电或热。
UCLASS()
class AETHERGAMEPLAY_API UAetherFireDamage : public UDamageType {GENERATED_BODY()};
// 连续环境暴露仍走火抗，但耐久按累计有效伤害结算，不能按 Tick 次数扣减。
UCLASS()
class AETHERGAMEPLAY_API UAetherHeatExposureDamage : public UAetherFireDamage {GENERATED_BODY()};
UCLASS()
class AETHERGAMEPLAY_API UAetherWaterDamage : public UDamageType {GENERATED_BODY()};
UCLASS()
class AETHERGAMEPLAY_API UAetherFrostDamage : public UDamageType {GENERATED_BODY()};
UCLASS()
class AETHERGAMEPLAY_API UAetherStormDamage : public UDamageType {GENERATED_BODY()};
