#pragma once
#include "CoreMinimal.h"
#include "AetherCombat.h"
#include "AetherProgression.h"
#include "AetherServices.h"
#include "AetherGuide.h"
#include "Interaction/AetherLiveTradeSession.h"
#include "Contracts/AetherInteraction.h"
#include "AetherFrontierCharacter.generated.h"

class AAetherFrontierProp;
class AAetherFrontierCharacter;
class UAetherPhysicsDamageComponent;
class UAetherTraversalComponent;
class UPhysicsHandleComponent;
class UInputMappingContext;
class UInputAction;
class UAetherFrontierPanel;
class UAetherMenuSubsystem;
DECLARE_MULTICAST_DELEGATE(FOnAetherPresentationChanged);
struct FAetherWorldPlacement;

// 角色身体与组件的生命周期入口；菜单由 LocalPlayer 子系统持有，此处保留旧玩法查询镜像。
UCLASS()
class AETHERLAB_API AAetherFrontierCharacter : public AAetherCharacter
{
    GENERATED_BODY()
public:
    AAetherFrontierCharacter(const FObjectInitializer& ObjectInitializer=FObjectInitializer::Get());
    bool CanStartLocomotion() const;
    virtual bool AllowsGeneratedMotion() const override;
    virtual bool CanJumpInternal_Implementation() const override;
    virtual void OnStartCrouch(float HalfHeightAdjust,float ScaledHalfHeightAdjust) override;
    virtual void OnEndCrouch(float HalfHeightAdjust,float ScaledHalfHeightAdjust) override;
    void StartJumpInput();
    void SetCrouchInput(bool Pressed);
    void SetSprintInput(bool Pressed);
    void ReleaseHeldInput();
    float CrouchCameraOffset=0;
    bool bAttackHeld=false;
    virtual void BeginPlay() override;
    virtual void PossessedBy(AController* C) override;
    virtual void UnPossessed() override;
    virtual void OnRep_PlayerState() override;
    virtual void SetupPlayerInputComponent(UInputComponent* I) override;
    virtual void Tick(float Dt) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual bool SpellUnlocked(int32 Spell) const override;
    virtual bool SkillUnlocked(const FString& SkillId) const override;
    virtual bool TrySpell(int32 Slot) override;
    virtual void GrantSpells() override;
    bool UsesNativeSkills() const;
    const FAetherSkillStateV10* NativeSkillView() const;
    virtual void ReceiveEquipmentHit_Implementation(const FAetherEquipmentHit& Hit) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UPhysicsHandleComponent> CarryHandle;
    UPROPERTY(Replicated) bool bSprinting = false;
    UPROPERTY(Replicated) TObjectPtr<AAetherFrontierProp> Carried;
    UPROPERTY(Replicated) TObjectPtr<AAetherFrontierCharacter> CompanionOwner;
    UPROPERTY(Replicated) bool bHealer = false;
    void ExecuteCompanionHeal(TWeakObjectPtr<AAetherFrontierCharacter> Target);
    UPROPERTY(Replicated) TObjectPtr<AAetherFrontierCharacter> ReviveTarget;
    TWeakObjectPtr<AAetherFrontierCharacter> RescueHolder;
    float RescueLeaseUntil=0;
    bool bFollowing=false;
    float ReviveStarted = 0;
    uint64 ReviveDamageSerial = 0;
    bool bDebugOverlay = false;
    void ToggleDebug(){bDebugOverlay=!bDebugOverlay;}
    void ToggleWeather(){ServerAction("Weather");}
    UAetherMenuSubsystem* MenuSubsystem() const;
    void SelectPanel(int32 NewPanel);
    void OpenPanel(int32 NewPanel);
    void ClosePanel();
    void MenuBack();
    FOnAetherPresentationChanged OnPresentationChanged;
    bool bPanel = false;
    int32 Panel = 0;
    float PressedAt = 0;
    float NextCompanionAction = 0;
    float NextServerAction = 0;
    float NextPotion = 0;
    bool bTravelPending=false;
    FVector TravelDestination,TravelOrigin;
    float TravelStarted=0;
    void BeginSafeTravel(FVector Destination);
    void UpdateSafeTravel();
    int32 SelectedItem = 0;
    FGuid SelectedInstance,MergeDestination;
    int32 InventoryQuantity=1,MinimumInventoryRevision=0;
    FAetherInventoryCommand PendingInventory;
    void SubmitInventory(FName Action,FName Definition=NAME_None);
    UFUNCTION(Server,Reliable) void ServerInventory(FAetherInventoryCommand Command);
    UFUNCTION(Server,Reliable) void ServerTradeInventory(FAetherInventoryCommand Command,FGuid Authorization);
    UFUNCTION(Client,Reliable) void ClientTradeOpened(FGuid Token,AAetherFrontierProp* Target,FName ShopId);
    UFUNCTION(Client,Reliable) void ClientTradeClosed(FGuid Token);
    UFUNCTION(Server,Reliable) void ServerCloseTrade(FGuid Token);
    bool OpenTrade(AAetherFrontierProp* Target);
    bool CanTradeWith(AAetherFrontierProp* Target) const;
    FName ActiveShop() const;
    bool AuthorizeTrade(FGuid Token,FName ShopId) const;
    void CloseTrade();
    void MaintainTrade();
    void RequestSale();
    FString SaleConfirmationText() const;
    FAetherLiveTradeSession TradeSession;
    FGuid PendingTradeAuthorization;
    FAetherSaleConfirmation SaleConfirmation;
    UFUNCTION(Client,Reliable) void InventoryResult(FGuid Id,EAetherInventoryResult Result,int32 Revision,int32 Transferred);
    FName TrackedQuest;
    UPROPERTY(Transient) TObjectPtr<UInputMappingContext> GameplayContext;
    UPROPERTY(Transient) TMap<FName,TObjectPtr<UInputAction>> InputActions;
    UFUNCTION(Exec) void AetherBind(FName Action,FKey Key);
    FKey BindingFor(FName Action) const;
    UPROPERTY() TObjectPtr<AAetherCharacter> LockedTarget;
    void ToggleLock();

    UPROPERTY(Replicated) FName CompanionId;
    UPROPERTY(Replicated) bool bCompanionHold = false;
    UPROPERTY(Replicated) FName EncounterId;
    UPROPERTY(Replicated) uint8 BossPhase = 0; // 0 armor, 1 overheat, 2 exposed
    UPROPERTY(Replicated) int32 BossVersion = 0;
    UPROPERTY(Replicated) float BossPhaseStarted = 0;
    UPROPERTY(Replicated) float BossPressure = 0;
    virtual float TakeDamage(float Amount,const FDamageEvent& Event,AController* EventInstigator,AActor* Causer) override;
    void CycleItem();
    void SellItem(){if(bPanel&&Panel==1)RequestSale();}
    void BuyMana(){if(bPanel&&Panel==1)SubmitInventory("Buy","ManaPotion");}
    void BuyRation(){if(bPanel&&Panel==1)SubmitInventory("Buy","Ration");}
    void UseMana(){if(!bPanel)SubmitInventory("Use","ManaPotion");}
    void Recover(){ServerAction("Recover");}
    void PartyCommand(){if(bPanel&&Panel==5)ServerAction("PartyCommand");}
    void Invite(){if(bPanel&&Panel==5)ServerAction("Invite");}
    void AcceptInvite(){if(bPanel&&Panel==5)ServerAction("AcceptInvite");}
    void LeaveParty(){if(bPanel&&Panel==5)ServerAction("LeaveParty");}
    void Dismiss(){if(bPanel&&Panel==5)ServerAction("Dismiss");}
    AAetherPlayerState* ProfileState() const;
    void BindPersistentAbilities();
    void ApplyProfileEquipment();
    UFUNCTION(Server,Reliable) void ServerAction(FName Action,int32 Index = 0);
    UFUNCTION(Server,Reliable) void ServerWorldService(FAetherWorldServiceCommand Command,AAetherFrontierProp* Target,FName ActionId);
    // 过渡期仍调用 v9 服务，但网络目标使用原 Actor 实例 + 持久 ID + 动作 + 所见档案版本。
    // Actor 的网络引用同时区分区域卸载后用同一持久 ID 重建的新实例。
    UFUNCTION(Server,Reliable) void ServerInteractTarget(AActor* Target,FName StableId,FName ActionId,int32 ExpectedProfileRevision);
    FAetherInteractionTarget InteractionFocus;
    TOptional<FAetherInteractionSelection> NativeInteractionFocus;
    bool bHasInteractionFocus=false;
    void RefreshInteractionFocus();
    UFUNCTION(Client,Reliable) void WorldServiceResult(FGuid Id,EAetherServiceResult Result,int32 Revision);
    UFUNCTION(Server,Reliable) void ServerSprint(bool Enabled);
    UFUNCTION(Client,Reliable) void Notify(const FString& Message);
    void ReleaseCarry();
    void CheckClosureClient(float Dt);
    UFUNCTION(Client,Reliable) void ClientClosureAction(FName Action,FRotator Look);
    UFUNCTION(Server,Reliable) void ServerClosureAck(int32 Phase,bool Passed);
    float ClosureClientTime=0;
    int32 ClosureSeenPhase=0;
private:
    void PressAttack(); void ReleaseAttack();
    void SprintOn(){SetSprintInput(true);} void SprintOff(){SetSprintInput(false);}
    void CrouchOn(){SetCrouchInput(true);} void CrouchOff(){SetCrouchInput(false);}
    void UsePotion(){if(!bPanel)SubmitInventory("Use","Potion");} void InteractV4();
    FAetherWorldServiceCommand PendingService;
    int32 MinimumServiceRevision=0;
    void Throw(){if(!bPanel)ServerAction("Throw");}
    void ClaimRewards();
    void Carry(){if(!bPanel)ServerAction("Carry");} void Push(){if(!bPanel)ServerAction("Push");}
    void EquipNext(){if(!bPanel)SubmitInventory("CycleMain");} void Shield(){if(!bPanel)SubmitInventory("CycleOff");}
    void SaveV4(){ServerAction("Save");} void Recruit(){ServerAction("Recruit");}
    void SplitStack(){if(bPanel&&Panel==1)SubmitInventory("Split");}
    void MergeStacks(){if(bPanel&&Panel==1)SubmitInventory("Merge");}
    void ToggleInventory(){SelectPanel(1);} void ToggleQuests(){SelectPanel(2);}
    void ToggleSkills(){SelectPanel(3);} void ToggleMap(){SelectPanel(4);}
    void ToggleParty(){SelectPanel(5);} void ToggleMenu(){MenuBack();}
    void CastSelectedV4(){if(!bPanel)TrySpell(SelectedSpell);}
    void Spell0(){SelectedSpell=0;} void Spell1(){SelectedSpell=1;} void Spell2(){SelectedSpell=2;} void Spell3(){SelectedSpell=3;}
    void Forward(float V); void Right(float V); void Yaw(float V); void Pitch(float V);
    void GuardOn(){if(!bPanel)ServerBlock(true);} void GuardOff(){ServerBlock(false);}
    void Dodge(){if(!bPanel)TryDodge();} void JumpV4(){StartJumpInput();}
};
