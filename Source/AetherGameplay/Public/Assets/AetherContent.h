#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "AetherEquipmentComponent.h"
#include "Framework/AetherAdventure.h"
#include "AetherContent.generated.h"

class USkeletalMesh;
class UAnimSequence;
UCLASS(BlueprintType)
class AETHERGAMEPLAY_API UAetherCharacterDefinition : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FName CharacterId;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<USkeletalMesh> BodyMesh;
    // 预览使用独立待机，不读取真实角色倒地/施法状态；体型不兼容时回到参考姿态并提示。
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<UAnimSequence> PreviewIdleAnimation =
        TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(TEXT("/Game/Characters/Mannequins/Anims/Unarmed/MM_Idle.MM_Idle")));
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<UAnimSequence> WalkAnimation;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<UAnimSequence> AttackAnimation;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FRotator MeshRotation = FRotator(0,-90,0);
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float CapsuleRadius = 34;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float CapsuleHalfHeight = 88;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FAetherEquippedSlot> InitialEquipment;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FName> QuickEquipItems;
    virtual FPrimaryAssetId GetPrimaryAssetId() const override { return FPrimaryAssetId(TEXT("AetherCharacter"),CharacterId); }
};
USTRUCT(BlueprintType)
struct FAetherLevelObject
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FAetherObjectSpec Spec;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FTransform Transform;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) double InitialWaterKg = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bInitiallyEnabled = true;
};
USTRUCT(BlueprintType)
struct FAetherLevelEnemy
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FName Id;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) uint8 Archetype = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FTransform Transform;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<UAetherCharacterDefinition> Definition;
};
UCLASS(BlueprintType)
class AETHERGAMEPLAY_API UAetherLevelDefinition : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FName LayoutId = TEXT("BrokenBell_Modular_v1");
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<UStaticMesh> StaticShell;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FTransform ShellTransform;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FTransform PlayerSpawn;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FAetherLevelObject> Objects;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FAetherLevelEnemy> Enemies;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float CourtEntryX = 300;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float ArenaEntryX = 3500;
    virtual FPrimaryAssetId GetPrimaryAssetId() const override { return FPrimaryAssetId(TEXT("AetherLevel"),LayoutId); }
};
// The composition root owns content references; reusable equipment and simulation modules do not.
UCLASS(BlueprintType)
class AETHERGAMEPLAY_API UAetherGameContent : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<UAetherEquipmentCatalog> EquipmentCatalog;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<UAetherLevelDefinition> Abbey;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<UAetherCharacterDefinition> Player;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<UAetherCharacterDefinition> Guard;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<UAetherCharacterDefinition> Caster;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<UAetherCharacterDefinition> Boss;
    static UAetherGameContent* Load(bool Basic = false);
};
