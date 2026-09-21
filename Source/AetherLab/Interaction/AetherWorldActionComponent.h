#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AetherWorldActionComponent.generated.h"
class AAetherFrontierCharacter;
class AAetherFrontierProp;
UENUM()
enum class EAetherWorldActionPhase:uint8 {Idle,Pickup,PutDown,Throw};
// 管理场景物体占用、接触前摇、提交和清理；角色只保留输入与生命周期装配。
UCLASS()
class AETHERLAB_API UAetherWorldActionComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UAetherWorldActionComponent();
    bool Begin(FName Action);
    void Release();
    bool IsBusy() const{return Phase!=EAetherWorldActionPhase::Idle;}
    AActor* ContactActor() const;
    virtual void TickComponent(float Delta,ELevelTick Type,FActorComponentTickFunction* Tick) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
private:
    UPROPERTY(Replicated) EAetherWorldActionPhase Phase=EAetherWorldActionPhase::Idle;
    UPROPERTY(Replicated) TObjectPtr<AAetherFrontierProp> Pending;
    float StartedAt=0;
    uint64 DamageSerial=0;
    UPROPERTY(Replicated) bool bCommitted=false;
    void Cancel();
    bool Reachable(AAetherFrontierCharacter& C,AAetherFrontierProp& P) const;
};
