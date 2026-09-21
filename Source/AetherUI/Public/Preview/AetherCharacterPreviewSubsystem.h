#pragma once
#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "Tickable.h"
#include "AetherCharacterPreviewSubsystem.generated.h"

class AAetherCharacter;
class AAetherCharacterPreviewActor;
class UAetherEquipmentComponent;
class UAetherMenuSubsystem;
class UTextureRenderTarget2D;
class UMaterialInterface;
class UMaterialInstanceDynamic;
struct FStreamableHandle;
struct FAetherPreviewAppearance;
struct FAetherPreviewSceneOwner;
// UHT 热重载构造器也可能触发成员销毁；删除操作在完整类型所在 .cpp 中执行。
struct AETHERUI_API FAetherPreviewSceneOwnerDeleter { void operator()(FAetherPreviewSceneOwner* Value) const; };
DECLARE_MULTICAST_DELEGATE(FOnAetherPreviewChanged);

// 每个 LocalPlayer 最多一个独立场景和一个渲染目标；不让每个格子/弹层创建第二套预览世界。
UCLASS(Config=Game)
class AETHERUI_API UAetherCharacterPreviewSubsystem : public ULocalPlayerSubsystem,public FTickableGameObject
{
    GENERATED_BODY()
public:
    UAetherCharacterPreviewSubsystem();
    virtual ~UAetherCharacterPreviewSubsystem() override;
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual bool IsTickable() const override;
    virtual TStatId GetStatId() const override;
    virtual UWorld* GetTickableGameObjectWorld() const override;
    void Acquire(UObject* Requester,AAetherCharacter* Character);
    void Release(UObject* Requester);
    UFUNCTION(BlueprintCallable) void Close();
    UFUNCTION(BlueprintCallable) void Rotate(float YawDelta,float PitchDelta);
    UFUNCTION(BlueprintCallable) void Zoom(float Delta);
    UFUNCTION(BlueprintCallable) void ResetView();
    UFUNCTION(BlueprintPure) bool IsPreviewActive() const {return bActive;}
    UFUNCTION(BlueprintPure) FText GetStatus() const {return Status;}
    UFUNCTION(BlueprintPure) UTextureRenderTarget2D* GetRenderTarget() const {return RenderTarget;}
    UFUNCTION(BlueprintPure) UMaterialInstanceDynamic* GetDisplayMaterial() const {return DisplayMaterial;}
    FOnAetherPreviewChanged OnChanged;
    UPROPERTY(Config,EditAnywhere) TSoftObjectPtr<UMaterialInterface> PreviewMaterial;
private:
    void SetSource(AAetherCharacter* Character);
    UFUNCTION() void RefreshAppearance();
    void HandleMenuContext();
    void ApplyLoaded(const FAetherPreviewAppearance& Appearance,uint64 Request);
    bool EnsureScene();
    TUniquePtr<FAetherPreviewSceneOwner,FAetherPreviewSceneOwnerDeleter> Scene;
    TSharedPtr<FStreamableHandle> LoadHandle;
    TWeakObjectPtr<AAetherCharacter> Source;
    TWeakObjectPtr<UObject> ViewOwner;
    TWeakObjectPtr<UAetherEquipmentComponent> EquipmentSource;
    TWeakObjectPtr<UAetherMenuSubsystem> Menu;
    UPROPERTY(Transient) TObjectPtr<AAetherCharacterPreviewActor> PreviewActor;
    UPROPERTY(Transient) TObjectPtr<UTextureRenderTarget2D> RenderTarget;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> DisplayMaterial;
    FText Status;
    uint64 RequestVersion=0;
    float OrbitYaw=0,OrbitPitch=0,ZoomFactor=1,SinceCapture=0;
    bool bActive=false,bReady=false;
    TSet<FString> ReportedWarnings;
};
