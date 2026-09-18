#pragma once
#include "CoreMinimal.h"
#include "ReactiveTypes.h"
#include "AetherLegacyFixture.generated.h"

// Frozen v6 tagged-property layout (38d5839). Do not derive this from the current record.
USTRUCT()
struct FAetherLegacyRecordV6
{
    GENERATED_BODY()
    UPROPERTY() FName StableId;
    UPROPERTY() FTransform Transform;
    UPROPERTY() bool bGateOpen = false;
    UPROPERTY() bool bHasMechanism = false;
    UPROPERTY() bool bSupportReleased = false;
    UPROPERTY() bool bSourceEnabled = true;
    UPROPERTY() double RemainingEnergyJ = 0;
    UPROPERTY() double SourceAge = 0;
    UPROPERTY() uint32 MaterialSignature = 0;
    UPROPERTY() double EnthalpyJ = 0;
    UPROPERTY() double WaterKg = 0;
    UPROPERTY() double ElectricalWaterKg = 0;
    UPROPERTY() double ElectricalWetness01 = 0;
    UPROPERTY() double FuelKg = 0;
    UPROPERTY() double Integrity = 1;
    UPROPERTY() double GasEnergyJ = 0;
    UPROPERTY() bool bBurning = false;
    UPROPERTY() bool bBroken = false;
    UPROPERTY() bool bBurst = false;
};
namespace AetherLegacyFixture
{
 bool DecodeRope(FReactiveSaveRecord& Record);
}
