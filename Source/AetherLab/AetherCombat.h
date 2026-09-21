#pragma once
#include "CoreMinimal.h"
#include "AbilitySystemInterface.h"
#include "AbilitySystemComponent.h"
#include "Combat/AetherAttributes.h"
#include "Combat/AetherCombatComponent.h"
#include "Abilities/AetherSpellAbility.h"
#include "Combat/AetherProjectile.h"
#include "GameFramework/Character.h"
#include "ReactiveBodyComponent.h"
#include "AetherEquipmentComponent.h"
#include "Animation/AetherActionPresentation.h"
#include "AetherCombat.generated.h"
class UAetherMotionComponent;

class UAetherResourceGate;
class UCameraComponent;
class USpringArmComponent;
class UTextRenderComponent;
class UStaticMeshComponent;
class UAetherCharacterDefinition;
DECLARE_MULTICAST_DELEGATE(FOnAetherCharacterAppearanceChanged);

UENUM()
enum class EAetherFighter : uint8 { Player, ShieldGuard, FireCaster, BellKnight, Wolf, Golem };

UCLASS()
class AETHERLAB_API AAetherCharacter : public ACharacter, public IAbilitySystemInterface, public IAetherHitReceiver
{
    GENERATED_BODY()
public:
    AAetherCharacter(const FObjectInitializer& ObjectInitializer=FObjectInitializer::Get());
    FOnAetherCharacterAppearanceChanged OnAppearanceChanged;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UAetherResourceGate> ResourceGate;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UAetherCombatComponent> CombatRuntime;
    // 只接收服务器战斗事实；不用客户端耐久数值或显示索引。
    void RecordEquipmentWear(bool Weapon,bool Guard);
    UPROPERTY(VisibleAnywhere) TObjectPtr<UAetherMotionComponent> Motion;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UAbilitySystemComponent> AbilitySystem;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UAetherAttributes> Attributes;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UReactiveBodyComponent> Reactive;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> BodyVisual;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UAetherEquipmentComponent> Equipment;
    UPROPERTY(ReplicatedUsing=ApplyCharacterDefinition, EditAnywhere, BlueprintReadOnly) TObjectPtr<UAetherCharacterDefinition> CharacterDefinition;
    UPROPERTY(VisibleAnywhere) TObjectPtr<USpringArmComponent> Arm;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UCameraComponent> Camera;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UTextRenderComponent> Nameplate;
    UPROPERTY(Replicated) EAetherFighter Fighter = EAetherFighter::Player;
    UPROPERTY(Replicated) bool bBlocking = false;
    UPROPERTY(Replicated) bool bWindingUp = false;
    UPROPERTY(Replicated) bool bPacified = false;
    UPROPERTY(Replicated) float MaxHealth = 100;
    float TimeSinceDamage() const {return CombatTime()-CombatRuntime->LastDamageAt;}
    // 交易等安全动作不能在刚完成攻击/施法后立即开放；初始零期限表示尚无战斗动作。
    bool HasRecentCombat(float Seconds) const
    {
        const float End=FMath::Max3(ActionUntil,CastLockUntil,StunUntil);
        return TimeSinceDamage()<Seconds||(End>0&&CombatTime()<End+Seconds);
    }
    UPROPERTY(Replicated) float WaterReserveKg = 3;
    UPROPERTY(Replicated) float CastLockUntil = 0;
    UPROPERTY(Replicated) float CastStartedAt = 0;
    UPROPERTY(Replicated) float StunUntil = 0;
    UPROPERTY(Replicated) FAetherActionPresentation PresentedAction;
    void PresentAction(FName Id,float Duration);
    int32 SelectedSpell = 0;
    FString Feedback;
    FVector Home = FVector::ZeroVector;
    virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override { return AbilitySystem; }
    virtual void BeginPlay() override;
    virtual void PossessedBy(AController* NewController) override;
    virtual void UnPossessed() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void OnRep_Controller() override;
    virtual void Tick(float Dt) override;
    virtual void SetupPlayerInputComponent(UInputComponent* Input) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    virtual float TakeDamage(float Amount, const FDamageEvent& Event, AController* EventInstigator, AActor* Causer) override;
    float Health() const { return Attributes->Health.GetCurrentValue(); }
    float Mana() const { return Attributes->Mana.GetCurrentValue(); }
    float Stamina() const { return Attributes->Stamina.GetCurrentValue(); }
    float MaximumMana() const {return FMath::Clamp(100.f+Attributes->GearMaxMana.GetCurrentValue(),1.f,100000.f);}
    float MaximumStamina() const {return FMath::Clamp(100.f+Attributes->GearMaxStamina.GetCurrentValue(),1.f,100000.f);}
    bool Alive() const { return Health() > 0 && !bPacified; }
    bool Ready() const;
    virtual bool AllowsGeneratedMotion() const;
    bool TryDodge();
    void RecordDodgeCommit();
    float CombatTime() const;
    bool FindSpellTarget(int32 Spell, FHitResult& Hit, FVector& Origin, FVector& Direction) const;
    bool ExecuteSpell(int32 Spell);
    // 旧编号接口仅供尚未迁移的快捷键/任务调用，内部立即转换为稳定身份。
    bool FindSkillTarget(const FString& SkillId,int32 Rank,FHitResult& Hit,FVector& Origin,FVector& Direction) const;
    bool ExecuteSkill(const FString& SkillId,int32 Rank);
    bool TrySkill(const FString& SkillId);
    virtual bool SkillUnlocked(const FString& SkillId) const;
    virtual void GrantSpells();
    virtual bool TrySpell(int32 Spell);
    virtual bool SpellUnlocked(int32 Spell) const { return true; }
    UPROPERTY(Replicated) bool bUseBasicAssets = false;
    void SetVitals(float HP, float MP, float SP);
    bool DeferEquipmentHit(const FAetherEquipmentHit& Hit);
    bool DeferDamage(float Amount,const FDamageEvent& Event,AController* Instigator,AActor* Causer);
    void AdvanceCombatResources(float Delta,double Temperature,TWeakObjectPtr<AActor> HeatSource);
    void ResetCombat();
    void ReceiveHit(float Damage, float PostureDamage, AAetherCharacter* Source, bool bCanBlock);
    void PerformMelee(bool bHeavy);
    bool RequestMelee(FName AttackId);
    void CancelActions();
    void ApplyPostureDamage(float Amount);
    FVector SafeMoveDirection(FVector Destination);
    float NextSteeringAt=0;
    FVector SteeringDirection=FVector::ZeroVector;
    TArray<FVector> NavigationPoints;
    FVector NavigationGoal=FVector::ZeroVector;
    int32 NavigationIndex=0;
    float NextPathAt=0;
    TWeakObjectPtr<AAetherCharacter> PerceivedTarget;
    FVector LastSeenPosition=FVector::ZeroVector;
    float LastSeenAt=-100;
    float NextPerceptionAt=0;
    virtual void ReceiveEquipmentHit_Implementation(const FAetherEquipmentHit& Hit) override;
    UFUNCTION() void ApplyCharacterDefinition();
    void Pacify();
    UFUNCTION(Server, Reliable) void ServerAttack(bool bHeavy);
    UFUNCTION(Server, Reliable) void ServerBlock(bool bValue);
    UFUNCTION(Server, Reliable) void ServerDodge();
    UFUNCTION(Server, Reliable) void ServerInteract(bool bAlternate);
    UFUNCTION(Server, Reliable) void ServerSave(bool bLoad);
private:
    void MoveForward(float V);
    void MoveRight(float V);
    void Turn(float V) { AddControllerYawInput(V); }
    void Look(float V) { AddControllerPitchInput(-V); }
    void Light() { ServerAttack(false); }
    void Heavy() { ServerAttack(true); }
    void BlockOn() { ServerBlock(true); }
    void BlockOff() { ServerBlock(false); }
    void CastSelected() { TrySpell(SelectedSpell); }
    void SelectHeat() { SelectedSpell = 0; }
    void SelectWater() { SelectedSpell = 1; }
    void SelectFrost() { SelectedSpell = 2; }
    void SelectLightning() { SelectedSpell = 3; }
    void Interact() { ServerInteract(false); }
    void Alternate() { ServerInteract(true); }
    void Save() { ServerSave(false); }
    void Load() { ServerSave(true); }
    void CycleEquipment();
    void ToggleShield();
    void StowWeapon() { Equipment->Unequip(TEXT("MainHand")); }
    void UpdateAnimation();
    void Think(float Dt);
    UFUNCTION() void Reaction(EReactiveReaction Kind, double Magnitude, FVector Vector);
    UFUNCTION() void ElectricalWindow(const FReactiveElectricalWindow& Window);
    UFUNCTION(Client, Reliable) void ClientFeedback(const FString& Message);
    TArray<FGameplayAbilitySpecHandle> SpellHandles;
    float ActionUntil = 0;
    float NextAI = 0;
    float NextShockStun = 0;
    uint32 PresentedAttackSerial = 0;
    bool bWalkingAnimation = false;
    bool bAIHeavy = false;
    bool bAbilitiesGranted = false;
    float NetProbeTime = 0;
    bool bNetCastRequested = false;
    bool bNetCostObserved = false;
    bool bNetEquipRequested = false;
    bool bNetProbeFinished = false;
    void NetworkProbe(float Dt);
};
