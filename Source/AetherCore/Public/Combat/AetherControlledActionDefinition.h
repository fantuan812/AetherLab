#pragma once
#include "CoreMinimal.h"
#include "AetherControlledActionDefinition.generated.h"

UENUM(BlueprintType)
enum class EAetherActionContactPolicy:uint8 {None,FixedObject,Weapon,Ground};
UENUM(BlueprintType)
enum class EAetherActionCancelPolicy:uint8 {PresentationOnly,CancelBeforeCommitKeepCommitted,AbilityOwned};
USTRUCT(BlueprintType)
struct AETHERCORE_API FAetherControlledActionDefinition
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadOnly) FName ActionId;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) int32 DefinitionVersion=1;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) float Duration=1;
    // -1 表示无玩法提交（循环姿态、受击等），不能把动画 Notify 解释成玩法提交。
    UPROPERTY(EditAnywhere,BlueprintReadOnly) float CommitTime=-1;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) float Cost=0;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) float Cooldown=0;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) float MotionSpeed=0;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) float MotionTime=0;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) float InvulnerabilityTime=0;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) float Impulse=0;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) bool bLoop=false;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) EAetherActionContactPolicy ContactPolicy=EAetherActionContactPolicy::None;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) EAetherActionCancelPolicy CancelPolicy=EAetherActionCancelPolicy::PresentationOnly;
    // 1 站立、2 蹲姿、4 搬运、8 倒地；动作互斥仍由 GAS/组件持有，不新增第二套状态机。
    UPROPERTY(EditAnywhere,BlueprintReadOnly) int32 AllowedStances=1;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) TArray<float> PhaseDurations;
    bool IsValid() const;
};
// One current source for authoritative timing and input policy; no compiled fallback catalog.
struct AETHERCORE_API FAetherControlledActionCatalog
{
    TArray<FAetherControlledActionDefinition> Actions;
    float AttackCharge=0,AttackBuffer=0;
    bool bValid=false;
    FString Error;
    static FAetherControlledActionCatalog Parse(const FString& Json);
    static const FAetherControlledActionCatalog& Get();
};
namespace AetherControlledActions
{
    AETHERCORE_API const TArray<FAetherControlledActionDefinition>& All();
    AETHERCORE_API const FAetherControlledActionDefinition* Find(FName Id);
}
