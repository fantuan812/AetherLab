#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Components/ActorComponent.h"
#include "UObject/Interface.h"
#include "AetherEquipmentComponent.generated.h"

class UStaticMesh;
struct FStreamableHandle;
class UAnimSequence;
class USkinnedMeshComponent;
class UStaticMeshComponent;

USTRUCT(BlueprintType)
struct AETHEREQUIPMENT_API FAetherAttackDefinition
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FName Id = TEXT("Light");
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TSoftObjectPtr<UAnimSequence> Animation;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float Damage = 16;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float PostureDamage = 14;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float ImpulseNs = 6;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0")) float CuttingWorkJ = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float StaminaCost = 8;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float ReachCm = 165;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float RadiusCm = 52;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float WindupSeconds = .08f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float ActiveSeconds = .12f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float RecoverySeconds = .18f;
    bool IsValid() const;
    float Duration() const { return WindupSeconds + ActiveSeconds + RecoverySeconds; }
};

UCLASS(BlueprintType)
class AETHEREQUIPMENT_API UAetherEquipmentDefinition : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FName ItemId;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FText DisplayName;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FName Slot = TEXT("MainHand");
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FName Socket = TEXT("hand_r");
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<UStaticMesh> Mesh;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FTransform GripTransform;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bOccupiesBothHands = false;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bAllowsGuard = false;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float GuardStaminaMultiplier = .8f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float ParryWindowSeconds = .16f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FAetherAttackDefinition> Attacks;
    virtual FPrimaryAssetId GetPrimaryAssetId() const override { return FPrimaryAssetId(TEXT("AetherEquipment"), ItemId); }
    const FAetherAttackDefinition* FindAttack(FName Id) const;
    bool IsValidDefinition() const;
};

UCLASS(BlueprintType)
class AETHEREQUIPMENT_API UAetherEquipmentCatalog : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<TObjectPtr<UAetherEquipmentDefinition>> Items;
    UAetherEquipmentDefinition* Find(FName Id) const;
    bool IsValidCatalog() const;
};

USTRUCT(BlueprintType)
struct AETHEREQUIPMENT_API FAetherEquippedSlot
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FName Slot;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FName ItemId;
};
USTRUCT(BlueprintType)
struct AETHEREQUIPMENT_API FAetherEquipmentHit
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) TObjectPtr<AActor> Source = nullptr;
    UPROPERTY(BlueprintReadOnly) FName ItemId;
    UPROPERTY(BlueprintReadOnly) FName AttackId;
    UPROPERTY(BlueprintReadOnly) float Damage = 0;
    UPROPERTY(BlueprintReadOnly) float PostureDamage = 0;
    UPROPERTY(BlueprintReadOnly) FVector ImpulseNs = FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly) float CuttingWorkJ = 0;
};
UINTERFACE(BlueprintType)
class AETHEREQUIPMENT_API UAetherHitReceiver : public UInterface { GENERATED_BODY() };
class AETHEREQUIPMENT_API IAetherHitReceiver
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable) void ReceiveEquipmentHit(const FAetherEquipmentHit& Hit);
};

UENUM(BlueprintType)
enum class EAetherAttackPhase : uint8 { Idle, Windup, Active, Recovery, Finished, Cancelled };

USTRUCT()
struct FAetherReplicatedAttack
{
    GENERATED_BODY()
    UPROPERTY() uint32 Serial = 0;
    UPROPERTY() FName ItemId;
    UPROPERTY() FName AttackId;
    UPROPERTY() float StartedAt = -100;
    UPROPERTY() bool bCancelled = false;
    UPROPERTY() EAetherAttackPhase Phase = EAetherAttackPhase::Idle;
};
DECLARE_DELEGATE_RetVal(bool, FAetherEquipmentCanAct);
DECLARE_DELEGATE_RetVal_OneParam(bool, FAetherEquipmentOwnsItem, FName);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FAetherLoadoutChanged);
DECLARE_DELEGATE_RetVal_OneParam(bool, FAetherAttackRequest, FName);
DECLARE_MULTICAST_DELEGATE_TwoParams(FAetherAttackFinished, uint32, bool);
DECLARE_MULTICAST_DELEGATE_TwoParams(FAetherAttackPhaseChanged, uint32, EAetherAttackPhase);

// This module knows no character class, quest, material solver, or weapon enum.
UCLASS(ClassGroup=(Aether), meta=(BlueprintSpawnableComponent))
class AETHEREQUIPMENT_API UAetherEquipmentComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UAetherEquipmentComponent();
    UPROPERTY(ReplicatedUsing=OnRep_Loadout, EditAnywhere, BlueprintReadOnly) TObjectPtr<UAetherEquipmentCatalog> Catalog;
    UPROPERTY(ReplicatedUsing=OnRep_Loadout, BlueprintReadOnly) TArray<FAetherEquippedSlot> Slots;
    UPROPERTY(ReplicatedUsing=OnRep_Loadout, BlueprintReadOnly) int32 LoadoutRevision = 0;
    UPROPERTY(BlueprintAssignable) FAetherLoadoutChanged OnLoadoutChanged;
    FAetherEquipmentCanAct CanAct;
    FAetherEquipmentOwnsItem OwnsItem;
    FAetherEquipmentCanAct CanContinueAttack;
    FAetherAttackRequest RequestAttack;
    FAetherAttackFinished OnAttackFinished;
    FAetherAttackPhaseChanged OnAttackPhaseChanged;
    bool bProfileManaged = false;
    UPROPERTY(Replicated) FAetherReplicatedAttack Attack;
    UFUNCTION(BlueprintCallable) bool Equip(FName ItemId);
    UFUNCTION(Server, Reliable) void ServerEquip(FName ItemId);
    UFUNCTION(BlueprintCallable) bool Unequip(FName Slot);
    UFUNCTION(Server, Reliable) void ServerUnequip(FName Slot);
    bool ValidateLoadout(const TArray<FAetherEquippedSlot>& Loadout) const;
    bool RestoreLoadout(const TArray<FAetherEquippedSlot>& Loadout);
    // Public requests always use the gameplay cost owner; equipment never debits attributes.
    bool StartAttack(FName AttackId);
    bool CanStartAttack(FName AttackId) const;
    bool BeginCommittedAttack(FName AttackId, FName ExpectedItem, int32 ExpectedRevision);
    void CancelAttack();
    bool IsBusy() const;
    bool IsAttackActive() const;
    float Clock() const;
    const FAetherAttackDefinition* CurrentAttack() const;
    UAetherEquipmentDefinition* InSlot(FName Slot) const;
    UAetherEquipmentDefinition* GuardDefinition() const;
    void SetAttachmentTarget(USkinnedMeshComponent* Mesh);
    UStaticMeshComponent* VisualForSlot(FName Slot) const;
    virtual void TickComponent(float Dt, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    uint64 AcceptedAttackCount = 0;
    uint64 AppliedHitCount = 0;
private:
    UFUNCTION() void OnRep_Loadout();
    TSharedPtr<FStreamableHandle> VisualLoad;
    TSet<FSoftObjectPath> FailedVisualAssets;
    void RebuildVisuals();
    void ResolveHits(const FAetherAttackDefinition& Definition);
    void FinishAttack(bool Cancelled);
    void SetAttackPhase(EAetherAttackPhase Phase);
    bool bAttackRunning = false;
    UPROPERTY(Transient) FAetherAttackDefinition ActiveDefinition;
    UPROPERTY(Transient) TObjectPtr<USkinnedMeshComponent> AttachmentTarget;
    UPROPERTY(Transient) TMap<FName,TObjectPtr<UStaticMeshComponent>> Visuals;
    TSet<TWeakObjectPtr<AActor>> HitActors;
    float LastAttackElapsed = -1;
};
