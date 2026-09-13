#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/HUD.h"
#include "ReactiveBodyComponent.h"
#include "ReactiveLabGameMode.generated.h"

class UStaticMeshComponent;
class UTextRenderComponent;
class UMaterialInstanceDynamic;

UCLASS()
class AETHERLAB_API AReactiveLabBody : public AActor
{
    GENERATED_BODY()
public:
    AReactiveLabBody();
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Mesh;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UReactiveBodyComponent> Reactive;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UTextRenderComponent> Label;
    FString DisplayName;
    FLinearColor BaseColor = FLinearColor::White;
    int32 ShockCount = 0;
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;
private:
    UFUNCTION() void HandleReaction(EReactiveReaction Reaction, double Magnitude, FVector Vector);
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> DynamicMaterial;
    double FlashUntil = 0;
};

UCLASS()
class AETHERLAB_API AReactiveLabController : public APlayerController
{
    GENERATED_BODY()
public:
    int32 SelectedSpell = 0;
    bool bRain = false;
    bool bWind = false;
    FString LastAction = TEXT("Select 1-5, then click a material. TAB runs the combined scenario.");
    AReactiveLabBody* GetPointedBody() const;
protected:
    virtual void BeginPlay() override;
    virtual void SetupInputComponent() override;
private:
    void Fire() { SelectedSpell = 0; }
    void Water() { SelectedSpell = 1; }
    void Lightning() { SelectedSpell = 2; }
    void Frost() { SelectedSpell = 3; }
    void Force() { SelectedSpell = 4; }
    void Cast();
    void ToggleRain();
    void ToggleWind();
    void RunScenario();
    void ResetLab();
};

UCLASS()
class AETHERLAB_API AReactiveLabHUD : public AHUD
{
    GENERATED_BODY()
public:
    virtual void DrawHUD() override;
};

UCLASS()
class AETHERLAB_API AReactiveLabGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    AReactiveLabGameMode();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;
    void RunScenario();
    UPROPERTY() TArray<TObjectPtr<AReactiveLabBody>> Samples;
private:
    AReactiveLabBody* SpawnSample(const FString& Name, EReactiveMaterialPreset Material, FVector Position,
        FVector Scale, double RadiusCm, FLinearColor Color, double WaterKg = 0, bool bSealed = false);
    double Elapsed = 0;
    bool bSmoke = false;
    bool bSmokeQueued = false;
    bool bSmokeFinished = false;
    bool bCapture = false;
    bool bCaptureRequested = false;
};
