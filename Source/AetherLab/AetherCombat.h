#pragma once
#include "CoreMinimal.h"
#include "AbilitySystemInterface.h"
#include "AbilitySystemComponent.h"
#include "AttributeSet.h"
#include "Abilities/GameplayAbility.h"
#include "GameFramework/Character.h"
#include "ReactiveBodyComponent.h"
#include "AetherEquipmentComponent.h"
#include "AetherCombat.generated.h"

class UCameraComponent;
class USpringArmComponent;
class UTextRenderComponent;
class UStaticMeshComponent;
class UAetherCharacterDefinition;

UCLASS()
class AETHERLAB_API UAetherAttributes : public UAttributeSet
{
    GENERATED_BODY()
public:
    UAetherAttributes();
    UPROPERTY(ReplicatedUsing=OnRep_Health) FGameplayAttributeData Health;
    UPROPERTY(ReplicatedUsing=OnRep_Mana) FGameplayAttributeData Mana;
    UPROPERTY(ReplicatedUsing=OnRep_Stamina) FGameplayAttributeData Stamina;
    UPROPERTY(ReplicatedUsing=OnRep_Posture) FGameplayAttributeData Posture;
    GAMEPLAYATTRIBUTE_PROPERTY_GETTER(UAetherAttributes, Health)
    GAMEPLAYATTRIBUTE_PROPERTY_GETTER(UAetherAttributes, Mana)
    GAMEPLAYATTRIBUTE_PROPERTY_GETTER(UAetherAttributes, Stamina)
    GAMEPLAYATTRIBUTE_PROPERTY_GETTER(UAetherAttributes, Posture)
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
private:
    UFUNCTION() void OnRep_Health(const FGameplayAttributeData& Old);
    UFUNCTION() void OnRep_Mana(const FGameplayAttributeData& Old);
    UFUNCTION() void OnRep_Stamina(const FGameplayAttributeData& Old);
    UFUNCTION() void OnRep_Posture(const FGameplayAttributeData& Old);
};

// 同一个 GA 可承载多个技能：Spec 标签决定 SkillId，Level 只表示实际等级。
UCLASS()
class AETHERLAB_API UAetherSpellAbility : public UGameplayAbility
{
    GENERATED_BODY()
public:
    UAetherSpellAbility();
    virtual bool CheckCost(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info, FGameplayTagContainer* Tags = nullptr) const override;
    virtual void ApplyCost(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info, FGameplayAbilityActivationInfo Activation) const override;
    virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
        FGameplayAbilityActivationInfo Activation, const FGameplayEventData* Event) override;
    static float Cost(int32 Spell);
};

UENUM()
enum class EAetherFighter : uint8 { Player, ShieldGuard, FireCaster, BellKnight, Wolf, Golem };

UCLASS()
class AETHERLAB_API AAetherCharacter : public ACharacter, public IAbilitySystemInterface, public IAetherHitReceiver
{
    GENERATED_BODY()
public:
    AAetherCharacter(const FObjectInitializer& ObjectInitializer=FObjectInitializer::Get());
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
    float TimeSinceDamage() const {return CombatTime()-LastDamageAt;}
    // 交易等安全动作不能在刚完成攻击/施法后立即开放；初始零期限表示尚无战斗动作。
    bool HasRecentCombat(float Seconds) const
    {
        const float End=FMath::Max3(ActionUntil,CastLockUntil,StunUntil);
        return TimeSinceDamage()<Seconds||(End>0&&CombatTime()<End+Seconds);
    }
    UPROPERTY(Replicated) float WaterReserveKg = 3;
    UPROPERTY(Replicated) float CastLockUntil = 0;
    UPROPERTY(Replicated) float StunUntil = 0;
    int32 SelectedSpell = 0;
    FString Feedback;
    FVector Home = FVector::ZeroVector;
    uint64 DamageReceivedCount = 0;
    TWeakObjectPtr<AActor> LastDamager;
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
    bool Alive() const { return Health() > 0 && !bPacified; }
    bool Ready() const;
    float CombatTime() const;
    bool FindSpellTarget(int32 Spell, FHitResult& Hit, FVector& Origin, FVector& Direction) const;
    bool ExecuteSpell(int32 Spell);
    // 旧编号接口仅供尚未迁移的快捷键/任务调用，内部立即转换为稳定身份。
    bool FindSkillTarget(const FString& SkillId,int32 Rank,FHitResult& Hit,FVector& Origin,FVector& Direction) const;
    bool ExecuteSkill(const FString& SkillId,int32 Rank);
    bool TrySkill(const FString& SkillId);
    bool SkillUnlocked(const FString& SkillId) const;
    void GrantSpells();
    bool TrySpell(int32 Spell);
    virtual bool SpellUnlocked(int32 Spell) const { return true; }
    UPROPERTY(Replicated) bool bUseBasicAssets = false;
    void SetVitals(float HP, float MP, float SP);
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
    float BlockStarted = -100;
    float NextParryAllowed = 0;
    float InvulnerableUntil = 0;
    float NextAI = 0;
    float LastDamageAt = -100;
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

UCLASS()
class AETHERLAB_API AAetherProjectile : public AActor
{
    GENERATED_BODY()
public:
    AAetherProjectile();
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Visual;
    UPROPERTY(Replicated) double HeatJ = 60000;
    FVector VelocityCm = FVector::ZeroVector;
    float Age = 0;
    virtual void BeginPlay() override;
    virtual void Tick(float Dt) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    static void IntegrateWeather(double& Heat, FVector& Velocity, const Reactive::FEnvironment& Weather, float Dt);
};
