#pragma once
#include "Subsystems/LocalPlayerSubsystem.h"
#include "Tickable.h"
#include "Interaction/AetherInteractionDefinitions.h"
#include "AetherDialogueSession.generated.h"
class AActor;
class AAetherFrontierCharacter;
class AAetherFrontierProp;
class APlayerController;
class ACameraActor;
DECLARE_MULTICAST_DELEGATE(FOnAetherDialogueChanged);

// 对话只持有本地弱引用和所见版本，所有服务选择仍交给服务器复验。
UCLASS()
class AETHERGAMEPLAY_API UAetherDialogueSession : public ULocalPlayerSubsystem,public FTickableGameObject
{
    GENERATED_BODY()
public:
    bool Open(AAetherFrontierCharacter& Player,AAetherFrontierProp& Target,const FAetherInteractionSelection& Selection,FString& Reason);
    void Close();
    bool Choose(int32 Index,uint64 ShownVersion,FString& Reason,FGuid* SubmittedCommandId=nullptr);
    bool Advance(uint64 ShownVersion);
    bool Skip(uint64 ShownVersion);
    EAetherDialoguePlaybackPhase GetPlaybackPhase() const{return Playback.Phase();}
    FString GetSubtitle() const;
    const FAetherDialoguePresentation* GetPresentation() const;
    const TOptional<FAetherDialogueView>& GetView() const{return View;}
    uint64 GetVersion() const{return Version;}
    FOnAetherDialogueChanged OnChanged;
    virtual void Tick(float Delta) override;
    virtual bool IsTickable() const override{return !HasAnyFlags(RF_ClassDefaultObject)&&(View.IsSet()||LocalCamera.IsValid());}
    virtual TStatId GetStatId() const override;
    virtual UWorld* GetTickableGameObjectWorld() const override{return GetWorld();}
    virtual void Deinitialize() override;
private:
    bool Refresh();
    bool ContextValid() const;
    bool BeginCamera(const FAetherDialoguePresentation& Presentation,FString& Reason);
    bool CameraPosition(FVector& Position,FRotator& Rotation) const;
    void TickCamera(float DeltaSeconds);
    void ReleaseCamera(bool Immediate);
    void DestroyCamera();
    void SurrenderCamera();
    AActor* RestoreTarget() const;
    TWeakObjectPtr<AAetherFrontierCharacter> Player;
    TWeakObjectPtr<AAetherFrontierProp> Target;
    TOptional<FAetherDialogueView> View;
    FAetherInteractionSelection Shown;
    FGuid Channel;
    FString Node,Signature;
    uint64 Version=0;
    FAetherDialoguePlayback Playback;
    uint64 DamageSerial=0;
    TWeakObjectPtr<APlayerController> OwningController,CameraController;
    TWeakObjectPtr<AActor> PreviousViewTarget,CameraPawn;
    TWeakObjectPtr<ACameraActor> LocalCamera;
    TOptional<FAetherDialogueCameraDefinition> Shot;
    bool bReturningCamera=false;
    double CameraElapsed=0;
    FVector CameraFromLocation=FVector::ZeroVector;
    FRotator CameraFromRotation=FRotator::ZeroRotator;
    float CameraFromFov=0;
};
