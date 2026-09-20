#pragma once
#include "Subsystems/LocalPlayerSubsystem.h"
#include "Tickable.h"
#include "Interaction/AetherInteractionDefinitions.h"
#include "AetherDialogueSession.generated.h"
class AAetherFrontierCharacter;
class AAetherFrontierProp;
DECLARE_MULTICAST_DELEGATE(FOnAetherDialogueChanged);

// 对话只持有本地弱引用和所见版本，所有服务选择仍交给服务器复验。
UCLASS()
class AETHERLAB_API UAetherDialogueSession : public ULocalPlayerSubsystem,public FTickableGameObject
{
    GENERATED_BODY()
public:
    bool Open(AAetherFrontierCharacter& Player,AAetherFrontierProp& Target,const FAetherInteractionSelection& Selection,FString& Reason);
    void Close();
    bool Choose(int32 Index,uint64 ShownVersion,FString& Reason);
    const TOptional<FAetherDialogueView>& GetView() const{return View;}
    uint64 GetVersion() const{return Version;}
    FOnAetherDialogueChanged OnChanged;
    virtual void Tick(float Delta) override;
    virtual bool IsTickable() const override{return !HasAnyFlags(RF_ClassDefaultObject)&&View.IsSet();}
    virtual TStatId GetStatId() const override;
    virtual UWorld* GetTickableGameObjectWorld() const override{return GetWorld();}
    virtual void Deinitialize() override;
private:
    bool Refresh();
    TWeakObjectPtr<AAetherFrontierCharacter> Player;
    TWeakObjectPtr<AAetherFrontierProp> Target;
    TOptional<FAetherDialogueView> View;
    FAetherInteractionSelection Shown;
    FGuid Channel;
    FString Node,Signature;
    uint64 Version=0;
    float PollElapsed=0;
};
