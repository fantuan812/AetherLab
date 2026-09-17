#include "ReactiveLabGameMode.h"
#include "ReactiveWorldSubsystem.h"
#include "Camera/CameraActor.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/InputComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/Canvas.h"
#include "Engine/DirectionalLight.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/SpectatorPawn.h"
#include "HAL/PlatformMisc.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UnrealClient.h"
#include "UObject/ConstructorHelpers.h"

AReactiveLabBody::AReactiveLabBody()
{
    PrimaryActorTick.bCanEverTick = true; PrimaryActorTick.TickInterval = 0.1f;
    Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh")); SetRootComponent(Mesh);
    Mesh->SetCollisionProfileName(TEXT("BlockAllDynamic"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (Cube.Succeeded()) Mesh->SetStaticMesh(Cube.Object);
    Reactive = CreateDefaultSubobject<UReactiveBodyComponent>(TEXT("Reactive"));
    Label = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Label")); Label->SetupAttachment(Mesh);
    Label->SetRelativeLocation(FVector(0,0,90)); Label->SetRelativeRotation(FRotator(0,-90,0));
    Label->SetHorizontalAlignment(EHTA_Center); Label->SetWorldSize(18); Label->SetTextRenderColor(FColor::White);
    Label->SetAbsolute(false, true, true);
}
void AReactiveLabBody::BeginPlay()
{
    Super::BeginPlay();
    if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")))
    {
        DynamicMaterial = UMaterialInstanceDynamic::Create(Base, this); Mesh->SetMaterial(0, DynamicMaterial);
        DynamicMaterial->SetVectorParameterValue(TEXT("Color"), BaseColor);
    }
    Mesh->SetMassOverrideInKg(NAME_None, float(Reactive->GetMaterial().DryMassKg));
    Reactive->OnReaction.AddDynamic(this, &AReactiveLabBody::HandleReaction);
}
void AReactiveLabBody::Tick(float Dt)
{
    Super::Tick(Dt);
    const FReactiveSnapshot& S = Reactive->State;
    const FString Flags = S.bBurst ? TEXT("BURST") : S.bBroken ? TEXT("BROKEN") : S.bBurning ? TEXT("BURNING") : S.IceFraction > 0.95 ? TEXT("FROZEN") : TEXT("");
    Label->SetText(FText::FromString(FString::Printf(TEXT("%s\n%.0f C | Water %.2f kg\nIntegrity %.0f%% %s"), *DisplayName,
        S.TemperatureC, S.WaterKg, S.Integrity * 100, *Flags)));
    FLinearColor Color = S.bBurning ? FLinearColor(1,0.08f,0.005f) : S.IceFraction > 0.1 ? FLinearColor(0.05f,0.7f,1) : BaseColor;
    if (GetWorld()->GetTimeSeconds() < FlashUntil) Color = FLinearColor::Yellow;
    if (DynamicMaterial) DynamicMaterial->SetVectorParameterValue(TEXT("Color"), Color);
    if (S.bBurning) DrawDebugPoint(GetWorld(), GetActorLocation() + FVector(0,0,80), 15, FColor::Orange, false, 0.12f);
}
void AReactiveLabBody::HandleReaction(EReactiveReaction Reaction, double Magnitude, FVector Vector)
{
    if (Reaction == EReactiveReaction::Shock) { ++ShockCount; FlashUntil = GetWorld()->GetTimeSeconds() + 0.7; }
    if (Reaction == EReactiveReaction::Burst) DrawDebugSphere(GetWorld(), GetActorLocation(), 400, 24, FColor::Orange, false, 2, 0, 3);
    if (Reaction == EReactiveReaction::Steam) DrawDebugSphere(GetWorld(), GetActorLocation() + FVector(0,0,100), 45, 8, FColor::White, false, 0.2f);
}
void AReactiveLabController::BeginPlay()
{
    Super::BeginPlay(); bShowMouseCursor = true; bEnableClickEvents = true;
    FInputModeGameAndUI Mode; Mode.SetHideCursorDuringCapture(false); SetInputMode(Mode);
    SetIgnoreLookInput(true); SetIgnoreMoveInput(true);
    GetWorldTimerManager().SetTimerForNextTick([this]()
    {
        const FVector Location(0,-1650,1550);
        if (ACameraActor* Camera = GetWorld()->SpawnActor<ACameraActor>(Location, (FVector(0,50,50) - Location).Rotation())) SetViewTarget(Camera);
    });
}
void AReactiveLabController::SetupInputComponent()
{
    Super::SetupInputComponent();
    InputComponent->BindKey(EKeys::One, IE_Pressed, this, &AReactiveLabController::Fire);
    InputComponent->BindKey(EKeys::Two, IE_Pressed, this, &AReactiveLabController::Water);
    InputComponent->BindKey(EKeys::Three, IE_Pressed, this, &AReactiveLabController::Lightning);
    InputComponent->BindKey(EKeys::Four, IE_Pressed, this, &AReactiveLabController::Frost);
    InputComponent->BindKey(EKeys::Five, IE_Pressed, this, &AReactiveLabController::Force);
    InputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &AReactiveLabController::Cast);
    InputComponent->BindKey(EKeys::R, IE_Pressed, this, &AReactiveLabController::ToggleRain);
    InputComponent->BindKey(EKeys::G, IE_Pressed, this, &AReactiveLabController::ToggleWind);
    InputComponent->BindKey(EKeys::Tab, IE_Pressed, this, &AReactiveLabController::RunScenario);
    InputComponent->BindKey(EKeys::BackSpace, IE_Pressed, this, &AReactiveLabController::ResetLab);
}
AReactiveLabBody* AReactiveLabController::GetPointedBody() const
{
    FHitResult Hit; GetHitResultUnderCursor(ECC_Visibility, false, Hit); return ::Cast<AReactiveLabBody>(Hit.GetActor());
}
void AReactiveLabController::Cast()
{
    AReactiveLabBody* Target = GetPointedBody(); if (!Target) { LastAction = TEXT("Point at one of the labeled material samples."); return; }
    FReactiveStimulus S;
    switch (SelectedSpell)
    {
    case 0: S.HeatJ = 60000; break;
    case 1: S.WaterKg = 0.5; break;
    case 2: S.ElectricalJ = 5000; break;
    case 3: S.HeatJ = -200000; break;
    default: S.ImpulseNs = FVector(0,10,0); break;
    }
    const bool Accepted = Target->Reactive->Inject(S);
    LastAction = FString::Printf(TEXT("%s -> %s"), Accepted ? TEXT("Stimulus accepted") : TEXT("Stimulus rejected"), *Target->DisplayName);
}
void AReactiveLabController::ToggleRain()
{
    bRain = !bRain;
    GetWorld()->GetSubsystem<UReactiveWorldSubsystem>()->SetWeather(20, bRain ? 0.05 : 0, bWind ? FVector(12,0,0) : FVector::ZeroVector);
}
void AReactiveLabController::ToggleWind()
{
    bWind = !bWind;
    GetWorld()->GetSubsystem<UReactiveWorldSubsystem>()->SetWeather(20, bRain ? 0.05 : 0, bWind ? FVector(12,0,0) : FVector::ZeroVector);
}
void AReactiveLabController::RunScenario()
{
    if (auto* Mode = GetWorld()->GetAuthGameMode<AReactiveLabGameMode>()) Mode->RunScenario();
    LastAction = TEXT("Scenario: fire spread, conductive puddle, freeze, sealed fuel burst. BACKSPACE resets.");
}
void AReactiveLabController::ResetLab() { UGameplayStatics::OpenLevel(this, TEXT("/Engine/Maps/Entry")); }
void AReactiveLabHUD::DrawHUD()
{
    Super::DrawHUD(); if (!Canvas) return;
    auto* PC = Cast<AReactiveLabController>(PlayerOwner); if (!PC) return;
    DrawRect(FLinearColor(0.015f,0.02f,0.04f,0.9f), 0, 0, Canvas->SizeX, 128);
    DrawText(TEXT("AETHER / REACTIVE WORLD LAB   |   UE 5.8"), FColor::White, 22, 12, nullptr, 1.5f);
    const TCHAR* Spells[] = { TEXT("HEAT +60 kJ"), TEXT("WATER +0.5 kg"), TEXT("ELECTRIC +5 kJ"), TEXT("HEAT -200 kJ"), TEXT("IMPULSE 10 N.s") };
    DrawText(FString::Printf(TEXT("1 Heat   2 Water   3 Lightning   4 Frost   5 Force    |    Selected: %s    |    Click to apply"), Spells[PC->SelectedSpell]), FColor::Cyan, 22, 42);
    DrawText(FString::Printf(TEXT("TAB Combined scenario   BACKSPACE Reset   R Rain [%s]   G Wind [%s]"), PC->bRain ? TEXT("ON") : TEXT("OFF"), PC->bWind ? TEXT("ON") : TEXT("OFF")), FColor::White, 22, 63);
    DrawText(PC->LastAction, FColor::Silver, 22, 84);
    if (auto* System = GetWorld()->GetSubsystem<UReactiveWorldSubsystem>()) DrawText(System->GetStatsText(), FColor::Green, 22, 105);
    if (AReactiveLabBody* Body = PC->GetPointedBody())
    {
        const auto& S = Body->Reactive->State;
        DrawRect(FLinearColor(0,0,0,0.85f), 10, Canvas->SizeY - 65, Canvas->SizeX - 20, 55);
        DrawText(FString::Printf(TEXT("%s | %.1f C | water %.3f kg | ice %.0f%% | fuel %.3f kg | integrity %.0f%% | pressure %.0f Pa"),
            *Body->DisplayName, S.TemperatureC, S.WaterKg, S.IceFraction*100, S.FuelKg, S.Integrity*100, S.GaugePressurePa), FColor::White, 22, Canvas->SizeY - 48);
    }
}
AReactiveLabGameMode::AReactiveLabGameMode()
{
    PrimaryActorTick.bCanEverTick = true; PlayerControllerClass = AReactiveLabController::StaticClass();
    HUDClass = AReactiveLabHUD::StaticClass(); DefaultPawnClass = ASpectatorPawn::StaticClass();
}
AReactiveLabBody* AReactiveLabGameMode::SpawnSample(const FString& Name, EReactiveMaterialPreset Material,
    FVector Position, FVector Scale, double Radius, FLinearColor Color, double Water, bool Sealed)
{
    AReactiveLabBody* Body = GetWorld()->SpawnActorDeferred<AReactiveLabBody>(AReactiveLabBody::StaticClass(), FTransform(Position), nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    Body->DisplayName = Name; Body->BaseColor = Color; Body->Reactive->Preset = Material;
    Body->Reactive->InteractionRadiusCm = Radius; Body->Reactive->InitialWaterKg = Water;
    Body->Mesh->SetWorldScale3D(Scale);
    if (Material == EReactiveMaterialPreset::Water)
    {
        Body->Reactive->bEnableChaosOnBreak = false; Body->Reactive->bIceControlsPawnCollision = true;
        Body->Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly); Body->Mesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
    }
    if (Sealed)
    {
        auto* Asset = NewObject<UReactiveMaterialAsset>(Body); Asset->Parameters.IgnitionC = 220;
        Asset->Parameters.InitialFuelKg = 0.1; Asset->Parameters.BurnRateKgPerSec = 0.004;
        Asset->Parameters.CombustionJPerKg = 42000000; Asset->Parameters.SealedVolumeM3 = 0.01;
        Body->Reactive->MaterialAsset = Asset;
    }
    UGameplayStatics::FinishSpawningActor(Body, FTransform(Position)); Samples.Add(Body); return Body;
}
void AReactiveLabGameMode::BeginPlay()
{
    Super::BeginPlay(); bSmoke = FParse::Param(FCommandLine::Get(), TEXT("AetherSmoke"));
    bCapture = FParse::Param(FCommandLine::Get(), TEXT("AetherCapture"));
    if (auto* Floor = GetWorld()->SpawnActor<AStaticMeshActor>(FVector(0,0,-35), FRotator::ZeroRotator))
    {
        Floor->SetMobility(EComponentMobility::Movable);
        Floor->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
        Floor->SetActorScale3D(FVector(20,15,0.5));
    }
    if (auto* Light = GetWorld()->SpawnActor<ADirectionalLight>(FVector(0,0,1000), FRotator(-55,-45,0)))
    { Light->GetLightComponent()->SetIntensity(7); }
    const FLinearColor Wood(0.28f,0.11f,0.035f), Metal(0.22f,0.32f,0.45f), Water(0.015f,0.15f,0.5f), Oil(0.15f,0.10f,0.015f);
    for (int32 I = 0; I < 4; ++I) SpawnSample(FString::Printf(TEXT("WOOD %d"), I+1), EReactiveMaterialPreset::Wood,
        FVector(-720 + I*150,-300,100), FVector(0.8,0.65,1.3), 60, Wood);
    SpawnSample(TEXT("CONDUCTIVE PUDDLE"), EReactiveMaterialPreset::Water, FVector(-480,300,20), FVector(4,3,0.15), 200, Water, 1);
    SpawnSample(TEXT("METAL / ENEMY A"), EReactiveMaterialPreset::Metal, FVector(-620,310,77.5), FVector(0.6,0.6,1), 60, Metal);
    SpawnSample(TEXT("METAL / ENEMY B"), EReactiveMaterialPreset::Metal, FVector(-450,310,77.5), FVector(0.6,0.6,1), 60, Metal);
    SpawnSample(TEXT("METAL / PLAYER"), EReactiveMaterialPreset::Metal, FVector(-300,450,77.5), FVector(0.6,0.6,1), 60, FLinearColor(0.1f,0.55f,0.2f));
    SpawnSample(TEXT("FREEZABLE WATER"), EReactiveMaterialPreset::Water, FVector(300,-300,25), FVector(2.8,2,0.15), 140, Water, 0.25);
    SpawnSample(TEXT("WET WOOD / IMPACT"), EReactiveMaterialPreset::Wood, FVector(570,-300,100), FVector(0.8,0.7,1.3), 60, Wood, 0.03);
    SpawnSample(TEXT("OPEN OIL"), EReactiveMaterialPreset::Oil, FVector(250,350,80), FVector(1,1,1), 60, Oil);
    SpawnSample(TEXT("SEALED FUEL"), EReactiveMaterialPreset::Oil, FVector(650,350,100), FVector(0.9,0.9,1.3), 60, Oil, 0, true);
    SpawnSample(TEXT("STONE"), EReactiveMaterialPreset::Stone, FVector(750,100,100), FVector(0.9,0.9,1), 60, FLinearColor(0.25f,0.25f,0.25f));
    UE_LOG(LogTemp, Display, TEXT("AETHER_LAB_READY samples=%d"), Samples.Num());
}
void AReactiveLabGameMode::RunScenario()
{
    if (Samples.Num() < 13) return;
    FReactiveStimulus Heat; Heat.HeatJ = 60000; Samples[0]->Reactive->Inject(Heat);
    Samples[10]->Reactive->Inject(Heat); Samples[11]->Reactive->Inject(Heat);
    FReactiveStimulus Electric; Electric.ElectricalJ = 5000; Samples[4]->Reactive->Inject(Electric);
    FReactiveStimulus Cold; Cold.HeatJ = -200000; Samples[8]->Reactive->Inject(Cold); Samples[9]->Reactive->Inject(Cold);
}
void AReactiveLabGameMode::Tick(float Dt)
{
    Super::Tick(Dt); if ((!bSmoke && !bCapture) || bSmokeFinished) return; Elapsed += Dt;
    if (bCapture && !bSmoke)
    {
        if (Elapsed > 8 && !bCaptureRequested)
        {
            bCaptureRequested = true;
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Screenshots/AetherLab.png"), false, false);
        }
        if (Elapsed > 10) { bSmokeFinished = true; FPlatformMisc::RequestExitWithStatus(false, 0); }
        return;
    }
    if (!bSmokeQueued && Elapsed > 0.3) { bSmokeQueued = true; RunScenario(); }
    if (Elapsed > 1.5)
    {
        bSmokeFinished = true;
        const auto* System = GetWorld()->GetSubsystem<UReactiveWorldSubsystem>();
        const bool Pass = Samples.Num() == 13 && Samples[0]->Reactive->State.bBurning
            && Samples[8]->Reactive->State.IceFraction > 0.95 && Samples[11]->Reactive->State.bBurst
            && System
            && Samples[5]->ShockCount > 0 && Samples[6]->ShockCount > 0 && Samples[7]->ShockCount > 0
            && System->GetSimulation()->GetStats().ElectricalDepositedJ > 0
            && System->GetSimulation()->GetStats().ElectricalDepositedJ + System->GetSimulation()->GetStats().ElectricalLostJ > 4999;
        UE_LOG(LogTemp, Display, TEXT("AETHER_SMOKE_%s | %s"), Pass ? TEXT("PASS") : TEXT("FAIL"), System ? *System->GetStatsText() : TEXT("No subsystem"));
        FPlatformMisc::RequestExitWithStatus(false, Pass ? 0 : 1);
    }
}
