#pragma once
#include "Components/ActorComponent.h"
#include "Effects/AetherBuffState.h"
#include "Inventory/AetherConsumableEffect.h"
#include "AetherBuffRuntime.generated.h"
class UAbilitySystemComponent;
USTRUCT()
struct FAetherMovementConfig
{
    GENERATED_BODY()
    UPROPERTY() float Speed=1;
    UPROPERTY() float ActionSpeed=1;
    UPROPERTY() uint32 Revision=0;
    UPROPERTY() double Time=0;
    bool NetSerialize(FArchive& Ar,UPackageMap*,bool& Success)
    {Ar<<Speed<<ActionSpeed<<Revision<<Time;Success=!Ar.IsError()&&FMath::IsFinite(Speed)&&Speed>=.1f&&Speed<=3&&FMath::IsFinite(ActionSpeed)&&ActionSpeed>=.1f&&ActionSpeed<=3&&FMath::IsFinite(Time);return Success;}
};
template<> struct TStructOpsTypeTraits<FAetherMovementConfig>:TStructOpsTypeTraitsBase2<FAetherMovementConfig>
{enum{WithNetSerializer=true};};
USTRUCT()
struct FAetherAttributePresentation
{
    GENERATED_BODY()
    UPROPERTY() FString AttributeId;
    UPROPERTY() FString SourceId;
    UPROPERTY() double Value=0;
    UPROPERTY() uint8 Operation=0;
};

USTRUCT()
struct FAetherBuffPresentation
{
    GENERATED_BODY()
    UPROPERTY() FGuid InstanceId;
    UPROPERTY() FString BuffId;
    UPROPERTY() FString Source;
    UPROPERTY() int32 Stacks=1;
    UPROPERTY() double ExpiresAt=0;
    UPROPERTY() bool bSuppressed=false;
};
USTRUCT()
struct FAetherEffectPresentationSnapshot
{
    GENERATED_BODY()
    UPROPERTY() FGuid LifeId;
    UPROPERTY() int64 ProfileRevision=-1;
    UPROPERTY() uint64 EffectRevision=0;
    UPROPERTY() uint64 ProjectionRevision=0;
    UPROPERTY() uint32 GrantRevision=0;
    UPROPERTY() TArray<FAetherBuffPresentation> Rows;
    UPROPERTY() bool bSilenced=false;
    UPROPERTY() bool bStunned=false;
    UPROPERTY() TMap<FString,double> Attributes;
    UPROPERTY() TArray<FAetherAttributePresentation> Contributions;
    UPROPERTY() TMap<FString,double> Cooldowns;
    UPROPERTY() float Health=0;
    UPROPERTY() float Mana=0;
    UPROPERTY() float Stamina=0;
    UPROPERTY() float MaxHealth=0;
    UPROPERTY() float MaxMana=0;
    UPROPERTY() float MaxStamina=0;
    UPROPERTY() uint8 SpellDenial=0;
    bool NetSerialize(FArchive& Ar,UPackageMap* Map,bool& Success);
};
template<> struct TStructOpsTypeTraits<FAetherEffectPresentationSnapshot>:TStructOpsTypeTraitsBase2<FAetherEffectPresentationSnapshot>
{enum{WithNetSerializer=true};};
UCLASS()
class AETHERGAMEPLAY_API UAetherBuffRuntime : public UActorComponent
{
    GENERATED_BODY()
public:
    UAetherBuffRuntime();
    virtual void TickComponent(float Delta,ELevelTick Type,FActorComponentTickFunction* Function) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    bool Apply(const FString& BuffId,const FString& Source,FString& Reason,FGuid DeliveryId={});
    bool ApplyRestBlessing(FString& Reason);
    bool ApplyDelivery(const FAetherConsumableEffectV10& Effect);
    bool CanApply(const FString& BuffId) const;
    bool Dispel(const FString& Tag,FString& Reason);
    void RemoveSource(const FString& Source);
    void FlushDue();
    void RefreshSnapshot();
    bool HasDue() const;
    bool HasTag(const FString& Tag) const;
    bool PresentationReady(int64 ProfileRevision) const;
    float MovementSpeedAt(double ServerTime) const;
    const FAetherBuffState& GetState() const{return State;}
    UPROPERTY(Replicated) FAetherEffectPresentationSnapshot Snapshot;
    float MoveSpeedMultiplier=1;
    float ActionSpeedMultiplier=1;
    uint32 MovementConfigRevision=0;
    double MovementConfigTime=0;
    UPROPERTY(ReplicatedUsing=OnRep_Movement) FAetherMovementConfig MovementConfig;
    UFUNCTION() void OnRep_Movement();
private:
    FAetherBuffState State;
    bool bPublishing=false;
    bool bPublicationPending=false;
    int32 PendingDueEvents=0;
    TOptional<FAetherBuffState> SchedulingState;
    bool Publish();
    void Advance(double Now);
    void PublishTags();
    TWeakObjectPtr<UAbilitySystemComponent> TagOwner;
    bool bOwnedSilence=false,bOwnedStun=false;
    struct FSpeedBoundary {double Time=0;float Speed=1;};
    TArray<FSpeedBoundary> SpeedHistory;
};
