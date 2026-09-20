#pragma once
#include "GameFramework/DamageType.h"
#include "AetherElementDamage.generated.h"

// 区分角色伤害结算渠道；仅改变生命损失，不抹掉已经守恒注入世界的水、电或热。
UCLASS()
class AETHERLAB_API UAetherFireDamage : public UDamageType {GENERATED_BODY()};
UCLASS()
class AETHERLAB_API UAetherWaterDamage : public UDamageType {GENERATED_BODY()};
UCLASS()
class AETHERLAB_API UAetherFrostDamage : public UDamageType {GENERATED_BODY()};
UCLASS()
class AETHERLAB_API UAetherStormDamage : public UDamageType {GENERATED_BODY()};
