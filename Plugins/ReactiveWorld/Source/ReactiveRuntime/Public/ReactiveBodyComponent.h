#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ReactiveTypes.h"
#include "ReactiveBodyComponent.generated.h"

class UPrimitiveComponent;
class UNiagaraComponent;
class UNiagaraSystem;

UCLASS(ClassGroup=(Reactive), meta=(BlueprintSpawnableComponent))
class REACTIVERUNTIME_API UReactiveBodyComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UReactiveBodyComponent();
    UPROPERTY(EditAnywhere, Category="Reactive") bool bParticipatesInSimulation = true;
    UPROPERTY(EditAnywhere, Category="Reactive|Persistence") FName StableId;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reactive") EReactiveMaterialPreset Preset = EReactiveMaterialPreset::Wood;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reactive") TObjectPtr<UReactiveMaterialAsset> MaterialAsset;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reactive", meta=(ClampMin="1", ClampMax="1000")) double InteractionRadiusCm = 50;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reactive", meta=(ClampMin="-200", ClampMax="100")) double InitialTemperatureC = 20;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reactive", meta=(ClampMin="0")) double InitialWaterKg = 0;
    // Only moving bodies are polled for transform changes by the subsystem.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reactive") bool bTrackMovement = false;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reactive|Bridges") bool bEnableChaosOnBreak = true;
    UPROPERTY(Replicated, EditAnywhere, BlueprintReadOnly, Category="Reactive|Bridges") bool bIceControlsPawnCollision = false;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reactive|Bridges") TObjectPtr<UNiagaraSystem> BurningEffect;
    UPROPERTY(ReplicatedUsing=OnRep_State, BlueprintReadOnly, Category="Reactive") FReactiveSnapshot State;
    UPROPERTY(BlueprintAssignable, Category="Reactive") FReactiveReactionEvent OnReaction;
    UFUNCTION(BlueprintCallable, Category="Reactive") bool Inject(const FReactiveStimulus& Stimulus);
    Reactive::FBodyId GetBodyId() const { return BodyId; }
    Reactive::FMaterial GetMaterial() const;
    UPrimitiveComponent* GetPrimitive() const;
    void AcceptState(const Reactive::FState& NewState);
    void AcceptEvent(const Reactive::FEvent& Event);
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    AActor* GetLastSourceActor() const { return LastSourceActor.Get(); }
    void RefreshPresentation();
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    UFUNCTION() void OnRep_State();
    TWeakObjectPtr<AActor> LastSourceActor;
    Reactive::FBodyId BodyId = Reactive::InvalidBody;
    UPROPERTY(Transient) TObjectPtr<UNiagaraComponent> FireVisual;
    ECollisionEnabled::Type InitialCollision = ECollisionEnabled::QueryOnly;
    ECollisionResponse InitialPawnResponse = ECR_Ignore;
};
