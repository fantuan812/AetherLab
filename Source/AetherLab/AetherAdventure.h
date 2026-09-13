#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/HUD.h"
#include "GameFramework/SaveGame.h"
#include "ReactiveBodyComponent.h"
#include "AetherEquipmentComponent.h"
#include "AetherAdventure.generated.h"

class AAetherCharacter;
class UStaticMeshComponent;
class UTextRenderComponent;
class UAetherLevelDefinition;

UENUM(BlueprintType)
enum class EAetherObjectKind : uint8 { Stone, Timber, Rope, Bridge, Water, Oil, Cistern, Apprentice, Record, Witness, Sigil, Return };
USTRUCT(BlueprintType)
struct FAetherObjectSpec
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FName Id;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString Label;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) EAetherObjectKind Kind = EAetherObjectKind::Stone;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector Scale = FVector::OneVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FLinearColor Color = FLinearColor(.25f,.27f,.3f);
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bInteractiveMaterial = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TSoftObjectPtr<UStaticMesh> ArtMesh;
};
UCLASS()
class AETHERLAB_API AAetherWorldObject : public AActor, public IAetherHitReceiver
{
    GENERATED_BODY()
public:
    AAetherWorldObject();
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Mesh;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UTextRenderComponent> Label;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UReactiveBodyComponent> Reactive;
    UPROPERTY(ReplicatedUsing=ApplySpec) FAetherObjectSpec Spec;
    UPROPERTY(Replicated) bool bEnabled = true;
    virtual void BeginPlay() override;
    virtual void Tick(float Dt) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    UFUNCTION() void ApplySpec();
    void ConfigureMaterial();
    void RefreshState();
    void ResetFragments();
    virtual void ReceiveEquipmentHit_Implementation(const FAetherEquipmentHit& Hit) override;
private:
    bool bFragmentsSpawned = false;
    TArray<TWeakObjectPtr<AActor>> Fragments;
};

USTRUCT()
struct FAetherQuest
{
    GENERATED_BODY()
    UPROPERTY() uint8 Phase = 0; // 0 arrive, 1 courtyard, 2 belfry, 3 resolved, 4 choice, 5 returned.
    UPROPERTY() bool bApprentice = false;
    UPROPERTY() bool bRecord = false;
    UPROPERTY() bool bTestimony = false;
    UPROPERTY() bool bGuardianPeace = false;
    UPROPERTY() uint8 Resolution = 0; // 0 unset, 1 keep at abbey, 2 carry home, 3 broken/manual bridge.
    UPROPERTY() bool bRewardGranted = false;
    UPROPERTY() int32 Crowns = 0;
};
UCLASS()
class AETHERLAB_API AAetherAdventureState : public AGameStateBase
{
    GENERATED_BODY()
public:
    virtual void BeginPlay() override;
    UPROPERTY(Replicated) FAetherQuest Quest;
    UPROPERTY(Replicated) bool bRain = false;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};

USTRUCT()
struct FAetherCharacterSave
{
    GENERATED_BODY()
    UPROPERTY() FName Id;
    UPROPERTY() float Health = 100;
    UPROPERTY() float Mana = 100;
    UPROPERTY() float Stamina = 100;
    UPROPERTY() float Water = 3;
    UPROPERTY() bool bPacified = false;
    UPROPERTY() TArray<FAetherEquippedSlot> Equipment;
};
UCLASS()
class AETHERLAB_API UAetherAdventureSave : public USaveGame
{
    GENERATED_BODY()
public:
    UPROPERTY() int32 Version = 2;
    UPROPERTY() FName Layout = TEXT("BrokenBell_01");
    UPROPERTY() FAetherQuest Quest;
    UPROPERTY() double AmbientTemperatureC = 20;
    UPROPERTY() double RainKgPerM2Sec = 0;
    UPROPERTY() FVector WindMPerSec = FVector::ZeroVector;
    UPROPERTY() TArray<FReactiveSaveRecord> World;
    UPROPERTY() TArray<FAetherCharacterSave> Characters;
};

UCLASS()
class AETHERLAB_API AAetherAdventureMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    AAetherAdventureMode();
    virtual void BeginPlay() override;
    virtual void Tick(float Dt) override;
    virtual void RestartPlayer(AController* NewPlayer) override;
    FString Interact(AAetherCharacter* Player, bool Alternate);
    FString SaveAdventure(AAetherCharacter* Player);
    FString LoadAdventure(AAetherCharacter* Player);
    AAetherWorldObject* FindObject(FName Id) const;
    UPROPERTY() TArray<TObjectPtr<AAetherWorldObject>> Objects;
    UPROPERTY() TArray<TObjectPtr<AAetherCharacter>> Enemies;
    UPROPERTY() TObjectPtr<AAetherCharacter> Boss;
    UPROPERTY() TObjectPtr<UAetherLevelDefinition> LevelDefinition;
    FString SaveSlot = TEXT("AetherAdventure_v2");
    bool bSmoke = false;
private:
    AAetherWorldObject* Object(FName Id, EAetherObjectKind Kind, FVector Position, FVector Scale, const FString& Label = FString(), double Water = 0);
    void BuildAbbey();
    void BuildArtAbbey();
    void UpdateWorld(float Dt);
    void SmokeStep();
    void EquipmentSmokeStep();
    bool bEquipmentSmoke = false;
    float EquipmentHealthBefore = 0;
    uint64 EquipmentHitsBefore = 0;
    float WaterTimer = 0;
    float Elapsed = 0;
    float SmokeWait = 0;
    int32 SmokeStage = 0;
    int32 SmokeFailures = 0;
    bool bNetSetup = false;
    bool bCapture = false;
    bool bCaptured = false;
    int32 CaptureStage = 0;
    void Check(bool Pass, const TCHAR* Name);
};
UCLASS()
class AETHERLAB_API AAetherModularAdventureMode : public AAetherAdventureMode
{
    GENERATED_BODY()
public:
    virtual void InitGame(const FString& MapName,const FString& Options,FString& ErrorMessage) override;
};
UCLASS()
class AETHERLAB_API AAetherAdventureHUD : public AHUD
{
    GENERATED_BODY()
public:
    virtual void DrawHUD() override;
};
