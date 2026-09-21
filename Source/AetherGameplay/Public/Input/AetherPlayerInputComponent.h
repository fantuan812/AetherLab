#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InputCoreTypes.h"
#include "AetherPlayerInputComponent.generated.h"
class UInputComponent;
class UInputAction;
class UInputMappingContext;
class UEnhancedInputComponent;
class UEnhancedInputLocalPlayerSubsystem;
// 本地身体的输入映射和绑定资源独占拥有者；不保存服务端玩法/菜单选择状态。
UCLASS()
class AETHERGAMEPLAY_API UAetherPlayerInputComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UAetherPlayerInputComponent();
    void Setup(UInputComponent* Input);
    void Detach();
    void SetBinding(FName Action,FKey Key);
    FKey BindingFor(FName Action) const;
    bool HasBindings() const{return !InputActions.IsEmpty()&&BoundInput.IsValid();}
    const TMap<FName,FKey>& Defaults() const{return DefaultBindings;}
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void TickComponent(float Delta,ELevelTick Type,FActorComponentTickFunction* Tick) override;
private:
    UPROPERTY(Transient) TObjectPtr<UInputMappingContext> GameplayContext;
    UPROPERTY(Transient) TMap<FName,TObjectPtr<UInputAction>> InputActions;
    TMap<FName,FKey> DefaultBindings;
    TArray<uint32> BindingHandles;
    TWeakObjectPtr<UEnhancedInputComponent> BoundInput;
    TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem> BoundSubsystem;
};
