#include "Combat/AetherCombat.h"
#include "AetherMotionComponent.h"
#include "Networking/AetherCommandRuntime.h"
#include "Definitions/AetherV10Definitions.h"
#include "Skills/AetherSkillAbilityBinding.h"
#include "Combat/AetherEquipmentMath.h"
#include "Combat/AetherControlledActionDefinition.h"
#include "Inventory/AetherResourceGate.h"
#include "Effects/AetherBuffRuntime.h"
#include "Equipment/AetherElementDamage.h"
#include "Skills/AetherSkillDefinitions.h"
#include "Skills/AetherNpcSkillDefinitions.h"
#include "AI/AetherNpcPerceptionDefinitions.h"
#include "Skills/AetherSkillCooldownState.h"
#include "Framework/AetherAdventure.h"
#include "Framework/AetherFrontier.h"
#include "Interaction/AetherActions.h"
#include "Movement/AetherDodgeAbility.h"
#include "Movement/AetherVaultAbility.h"
#include "Interaction/AetherWorldActionComponent.h"
#include "Movement/AetherTraversal.h"
#include "Movement/AetherNavigationProbe.h"
#include "Animation/AetherAnimation.h"
#include "Assets/AetherContent.h"
#include "Assets/AetherAssetPreload.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "AIController.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "NavigationInvokerComponent.h"
#include "ReactiveWorldSubsystem.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/SpringArmComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/PlatformMisc.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
void Tint(UStaticMeshComponent* Mesh, FLinearColor Color)
{
    auto* Mat = Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0));
    if (!Mat) Mat = Mesh->CreateAndSetMaterialInstanceDynamic(0);
    if (Mat) Mat->SetVectorParameterValue(TEXT("Color"), Color);
}
}
AAetherCharacter::AAetherCharacter(const FObjectInitializer& ObjectInitializer):Super(ObjectInitializer)
{
    PrimaryActorTick.bCanEverTick = true; bReplicates = true; SetReplicateMovement(true);
    AIControllerClass = AAIController::StaticClass();
    CreateDefaultSubobject<UNavigationInvokerComponent>(TEXT("LocalNavigation"))->SetGenerationRadii(2400,3200);
    SetNetUpdateFrequency(20); SetNetCullDistanceSquared(FMath::Square(12000.f));
    GetCapsuleComponent()->InitCapsuleSize(34, 88); GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
    bUseControllerRotationYaw = true;
    GetCharacterMovement()->MaxWalkSpeed = 450; GetCharacterMovement()->JumpZVelocity = 420;
    Arm = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraArm")); Arm->SetupAttachment(RootComponent);
    Arm->TargetArmLength = 430; Arm->SocketOffset = FVector(0, 55, 80); Arm->bUsePawnControlRotation = true;
    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera")); Camera->SetupAttachment(Arm);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    BodyVisual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Surcoat")); BodyVisual->SetupAttachment(RootComponent);
    BodyVisual->SetStaticMesh(Cube.Object); BodyVisual->SetRelativeScale3D(FVector(.5,.6,1.45)); BodyVisual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    ResourceGate=CreateDefaultSubobject<UAetherResourceGate>(TEXT("ResourceGate"));
    BuffRuntime=CreateDefaultSubobject<UAetherBuffRuntime>(TEXT("BuffRuntime"));
    CombatRuntime=CreateDefaultSubobject<UAetherCombatComponent>(TEXT("CombatRuntime"));
    Motion=CreateDefaultSubobject<UAetherMotionComponent>(TEXT("GeneratedMotion"));
    Equipment = CreateDefaultSubobject<UAetherEquipmentComponent>(TEXT("Equipment"));
    Equipment->ModifyHit.BindWeakLambda(this,[this](FAetherEquipmentHit& Hit){
        if(!Attributes)return;
        Hit.Damage=AetherEquipmentMath::Attack(Hit.Damage,Attributes->GearDamage.GetCurrentValue());
        Hit.PostureDamage=AetherEquipmentMath::Attack(Hit.PostureDamage,Attributes->GearPosture.GetCurrentValue());
    });
    GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Nameplate = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Nameplate")); Nameplate->SetupAttachment(RootComponent);
    Nameplate->SetRelativeLocation(FVector(0,0,120)); Nameplate->SetHorizontalAlignment(EHTA_Center); Nameplate->SetWorldSize(22); Nameplate->SetTextRenderColor(FColor::White);
    AbilitySystem = CreateDefaultSubobject<UAetherDefinitionAbilitySystem>(TEXT("Abilities")); AbilitySystem->SetIsReplicated(true);
    AbilitySystem->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);
    Attributes = CreateDefaultSubobject<UAetherAttributes>(TEXT("Attributes"));
    Reactive = CreateDefaultSubobject<UReactiveBodyComponent>(TEXT("Reactive"));
    Reactive->bElectricalTerminal=true; Reactive->ReceiverLoad=1; Reactive->ReceiverCapacityJ=12000;
    Reactive->bTrackMovement = true; Reactive->bEnableChaosOnBreak = false; Reactive->InteractionRadiusCm = 85;
    // A small exposed material patch, not a simulation of the entire human body's heat capacity.
    auto* Patch = CreateDefaultSubobject<UReactiveMaterialAsset>(TEXT("ExposedPatch"));
    Patch->Parameters.DryMassKg = .5; Patch->Parameters.SpecificHeatJPerKgK = 2000;
    Patch->Parameters.WaterCapacityKg = .5; Patch->Parameters.InitialFuelKg = .02;
    Patch->Parameters.IgnitionC = 180; Patch->Parameters.Conductivity = .12;
    Patch->Parameters.CoolingWPerK = 25; Patch->Parameters.StrengthNs = 1000;
    Reactive->MaterialAsset = Patch;
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Surface(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    if (Surface.Succeeded()) BodyVisual->SetMaterial(0,Surface.Object);
}
void AAetherCharacter::BeginPlay()
{
    if(HasAuthority())MaxHealth=Fighter==EAetherFighter::BellKnight?320:Fighter==EAetherFighter::Golem?160:Fighter==EAetherFighter::Player?100:80;
    Super::BeginPlay(); Home = GetActorLocation(); AbilitySystem->InitAbilityActorInfo(AbilitySystem->GetOwner(), this);
    Equipment->CanAct.BindUObject(this,&AAetherCharacter::Ready);
    Equipment->OnAuthoritativeContact.BindWeakLambda(this,[this]{RecordEquipmentWear(true,false);});
    Equipment->ActionSpeed.BindWeakLambda(this,[this]{return BuffRuntime?BuffRuntime->ActionSpeedMultiplier:1.f;});
    Equipment->RequestAttack.BindUObject(this,&AAetherCharacter::RequestMelee);
    Equipment->CanContinueAttack.BindLambda([this](){return Alive()&&CombatTime()>=StunUntil&&AbilitySystem&&AbilitySystem->GetAvatarActor()==this;});
    Equipment->AddTickPrerequisiteActor(this);
    if (auto* Content=UAetherGameContent::Load(bUseBasicAssets))
    {
        Equipment->Catalog=Content->EquipmentCatalog;
        if (HasAuthority() && !CharacterDefinition)
            CharacterDefinition=Fighter==EAetherFighter::Player?Content->Player:(Fighter==EAetherFighter::BellKnight||Fighter==EAetherFighter::Golem)?Content->Boss:Fighter==EAetherFighter::FireCaster?Content->Caster:Content->Guard;
    }
    ApplyCharacterDefinition();
    if (HasAuthority() && CharacterDefinition && !Equipment->RestoreLoadout(CharacterDefinition->InitialEquipment))
        UE_LOG(LogTemp,Error,TEXT("Invalid initial loadout for %s"),*GetName());
    if (Fighter == EAetherFighter::Player && GetNetMode() == NM_Standalone && !bUseBasicAssets) Reactive->StableId = TEXT("Player");
    Reactive->OnReaction.AddDynamic(this, &AAetherCharacter::Reaction);
    Reactive->OnElectricalWindow.AddDynamic(this,&AAetherCharacter::ElectricalWindow);
    if (HasAuthority())
    {
        GrantSpells(); if(!ResourceGate->IsRecovering())SetVitals(MaxHealth, 100, 100);
        if (Fighter != EAetherFighter::Player && !Controller) SpawnDefaultController();
    }
    if (!IsA<AAetherFrontierCharacter>() && IsLocallyControlled() && Fighter == EAetherFighter::Player)
        if (auto* PC = Cast<APlayerController>(Controller)) { PC->SetInputMode(FInputModeGameOnly()); PC->bShowMouseCursor = false; }
}
void AAetherCharacter::ApplyCharacterDefinition()
{
    if (!CharacterDefinition) return;
    if (auto* Body=bUseBasicAssets?CharacterDefinition->BodyMesh.Get():CharacterDefinition->BodyMesh.LoadSynchronous())
    {
        GetMesh()->SetSkeletalMesh(Body); BodyVisual->SetVisibility(false);
        GetCapsuleComponent()->SetCapsuleSize(CharacterDefinition->CapsuleRadius,CharacterDefinition->CapsuleHalfHeight);
        GetMesh()->SetRelativeLocation(FVector(0,0,-CharacterDefinition->CapsuleHalfHeight));
        GetMesh()->SetRelativeRotation(CharacterDefinition->MeshRotation);
        if(bUseBasicAssets&&GetNetMode()!=NM_DedicatedServer)
        {
            auto* Class=CharacterDefinition->AnimationClass.Get();
            if(!Class)UE_LOG(LogTemp,Error,TEXT("AETHER_CHARACTER_ANIMBP_MISSING %s"),*CharacterDefinition->AnimationClass.ToString());
            GetMesh()->SetAnimationMode(EAnimationMode::AnimationBlueprint);GetMesh()->SetAnimInstanceClass(Class?Class:UAetherAnimInstance::StaticClass());
        }
        Equipment->SetAttachmentTarget(GetMesh());
        Nameplate->SetRelativeLocation(FVector(0,0,CharacterDefinition->CapsuleHalfHeight+35));
        OnAppearanceChanged.Broadcast();
    }
}
void AAetherCharacter::OnStartCrouch(float HeightAdjust,float ScaledHeightAdjust)
{
    Super::OnStartCrouch(HeightAdjust,ScaledHeightAdjust);
    // Super 直接改写 RelativeLocation，需先同步缓存；否则世界位置恰好相同时 SetRelativeLocation 会提前返回。
    GetMesh()->UpdateComponentToWorld();
    // 引擎默认回调从 CDO 读取 Mesh 高度，本项目外观却在 BeginPlay 从角色定义加载。
    // 按本角色的站立胶囊高度恢复偏移，否则蹲下会把 Mesh 抬高 88 cm，起身后仍然悬空。
    const float StandingHalf=CharacterDefinition?CharacterDefinition->CapsuleHalfHeight:88.f;
    FVector Offset=GetMesh()->GetRelativeLocation();Offset.Z=-StandingHalf+HeightAdjust;
    GetMesh()->SetRelativeLocation(Offset);
    CacheInitialMeshOffset(Offset,GetMesh()->GetRelativeRotation());
}
void AAetherCharacter::OnEndCrouch(float HeightAdjust,float ScaledHeightAdjust)
{
    Super::OnEndCrouch(HeightAdjust,ScaledHeightAdjust);
    GetMesh()->UpdateComponentToWorld();
    const float StandingHalf=CharacterDefinition?CharacterDefinition->CapsuleHalfHeight:88.f;
    FVector Offset=GetMesh()->GetRelativeLocation();Offset.Z=-StandingHalf;
    GetMesh()->SetRelativeLocation(Offset);
    CacheInitialMeshOffset(Offset,GetMesh()->GetRelativeRotation());
}
void AAetherCharacter::CycleEquipment()
{
    if (!CharacterDefinition || CharacterDefinition->QuickEquipItems.IsEmpty()) return;
    auto* Current=Equipment->InSlot(TEXT("MainHand"));
    int32 Index=Current?CharacterDefinition->QuickEquipItems.Find(Current->ItemId):INDEX_NONE;
    Equipment->Equip(CharacterDefinition->QuickEquipItems[(Index+1)%CharacterDefinition->QuickEquipItems.Num()]);
}
void AAetherCharacter::ToggleShield()
{
    if (Equipment->InSlot(TEXT("OffHand"))) Equipment->Unequip(TEXT("OffHand"));
    else if (CharacterDefinition) for (const auto& S:CharacterDefinition->InitialEquipment) if (S.Slot==TEXT("OffHand")) { Equipment->Equip(S.ItemId); break; }
}
void AAetherCharacter::UpdateAnimation()
{
    if (!CharacterDefinition || GetNetMode()==NM_DedicatedServer || Cast<UAetherAnimInstance>(GetMesh()->GetAnimInstance())) return;
    if (Equipment->IsBusy())
    {
        if (PresentedAttackSerial!=Equipment->Attack.Serial)
        {
            PresentedAttackSerial=Equipment->Attack.Serial;
            if (auto* A=CharacterDefinition->AttackAnimation.Get())
            {
                GetMesh()->PlayAnimation(A,false);
                if (const auto* D=Equipment->CurrentAttack()) GetMesh()->SetPlayRate(A->GetPlayLength()/D->Duration());
                GetMesh()->SetPosition(FMath::Max(0.f,Equipment->Clock()-Equipment->Attack.StartedAt));
            }
            bWalkingAnimation=false;
        }
        return;
    }
    const bool Walk=Alive()&&GetVelocity().SizeSquared2D()>100;
    if (Walk && !bWalkingAnimation)
    { if (auto* A=CharacterDefinition->WalkAnimation.Get()) { GetMesh()->PlayAnimation(A,true); GetMesh()->SetPlayRate(1); } bWalkingAnimation=true; }
    else if (!Walk)
    {
        if(bUseBasicAssets)
        {
            auto* Idle=FindObject<UAnimSequence>(nullptr,TEXT("/Game/Characters/Mannequins/Anims/Unarmed/MM_Idle.MM_Idle"));
            auto* Node=GetMesh()->GetSingleNodeInstance();
            if(Idle&&(!Node||Node->GetCurrentAsset()!=Idle)){GetMesh()->PlayAnimation(Idle,true);GetMesh()->SetPlayRate(1);}
        }
        else{GetMesh()->Stop();GetMesh()->SetPosition(0);}
        bWalkingAnimation=false;
    }
}
void AAetherCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AAetherCharacter, MaxHealth); DOREPLIFETIME(AAetherCharacter, Fighter);DOREPLIFETIME(AAetherCharacter,SkillAuthority);DOREPLIFETIME(AAetherCharacter,SkillLoadoutId); DOREPLIFETIME(AAetherCharacter, bBlocking);
    DOREPLIFETIME(AAetherCharacter, bWindingUp); DOREPLIFETIME(AAetherCharacter, bPacified);
    DOREPLIFETIME(AAetherCharacter, PresentedAction);
    DOREPLIFETIME(AAetherCharacter, WaterReserveKg); DOREPLIFETIME(AAetherCharacter, CastStartedAt); DOREPLIFETIME(AAetherCharacter, CastLockUntil); DOREPLIFETIME(AAetherCharacter, StunUntil);
    DOREPLIFETIME_CONDITION(AAetherCharacter,CastExecutionId,COND_OwnerOnly);
    DOREPLIFETIME(AAetherCharacter, CharacterDefinition); DOREPLIFETIME(AAetherCharacter,bUseBasicAssets);
}
void AAetherCharacter::PossessedBy(AController* C)
{ CancelActions();EnemySkillDecision.Reset();EnemyPerception.Reset(); Super::PossessedBy(C); AbilitySystem->InitAbilityActorInfo(AbilitySystem->GetOwner(),this); }
void AAetherCharacter::CancelActions()
{
    if(!HasAuthority())return;
    EnemySkillDecision.CancelRequest(CastExecutionId);
    Equipment->CancelAttack();
    // A PlayerState ASC may already have moved to a replacement pawn.
    if(AbilitySystem&&AbilitySystem->GetAvatarActor()==this)AbilitySystem->CancelAllAbilities();
}
void AAetherCharacter::UnPossessed()
{
    CancelActions();EnemySkillDecision.Reset();EnemyPerception.Reset();
    if(AbilitySystem&&AbilitySystem->GetAvatarActor()==this)AbilitySystem->ClearActorInfo();
    Super::UnPossessed();
}
void AAetherCharacter::EndPlay(const EEndPlayReason::Type Reason)
{
    CancelActions();EnemySkillDecision.Reset();EnemyPerception.Reset();
    if(AbilitySystem&&AbilitySystem->GetAvatarActor()==this)AbilitySystem->ClearActorInfo();
    Super::EndPlay(Reason);
}
void AAetherCharacter::OnRep_Controller()
{ Super::OnRep_Controller(); AbilitySystem->InitAbilityActorInfo(AbilitySystem->GetOwner(),this); }
void AAetherCharacter::GrantCoreAbilities()
{
    if(HasAuthority())
    {
        if(!AbilitySystem->FindAbilitySpecFromClass(UAetherVaultAbility::StaticClass()))AbilitySystem->GiveAbility(FGameplayAbilitySpec(UAetherVaultAbility::StaticClass(),1,10,this));
        if(!AbilitySystem->FindAbilitySpecFromClass(UAetherDodgeAbility::StaticClass()))AbilitySystem->GiveAbility(FGameplayAbilitySpec(UAetherDodgeAbility::StaticClass(),1,9,this));
        if(!AbilitySystem->FindAbilitySpecFromClass(UAetherReviveAbility::StaticClass()))AbilitySystem->GiveAbility(FGameplayAbilitySpec(UAetherReviveAbility::StaticClass(),1,8,this));
        for(int32 Level=1;Level<=2;++Level)
        {bool Found=false;for(const auto& Spec:AbilitySystem->GetActivatableAbilities())if(Spec.Ability&&Spec.Ability->IsA<UAetherMeleeAbility>()&&Spec.Level==Level)Found=true;
        if(!Found)AbilitySystem->GiveAbility(FGameplayAbilitySpec(UAetherMeleeAbility::StaticClass(),Level,4+Level,this));}
    }
}
void AAetherCharacter::GrantSpells()
{
    GrantCoreAbilities();FString Reason;
    if(SkillAuthority==EAetherSkillAuthority::Definition&&!GrantDefinitionSkills(Reason))
        UE_LOG(LogTemp,Error,TEXT("AETHER_NPC_SKILLS_UNAVAILABLE actor=%s reason=%s"),*GetName(),*Reason);
}
bool AAetherCharacter::SkillUnlocked(const FString& SkillId) const
{
    if(SkillAuthority!=EAetherSkillAuthority::Definition||GetPlayerState<AAetherPlayerState>()||!AbilitySystem)return false;
    const auto* Loadout=FAetherNpcSkillDefinitions::Get().Find(SkillLoadoutId);
    const auto* Grant=Loadout?Loadout->InitialGrants.FindByPredicate([&](const auto& R){return R.SkillId.Equals(SkillId,ESearchCase::CaseSensitive);}):nullptr;
    const auto* Spec=Grant?AetherSkillBinding::Find(*AbilitySystem,SkillId):nullptr;
    return Spec&&FAetherSkillDefinitionsV10::Get().Effect(SkillId,Spec->Level);
}
bool AAetherCharacter::SpellUnlocked(int32 Slot) const
{const FString Id=SkillAtInputSlot(Slot);return !Id.IsEmpty()&&SkillUnlocked(Id);}
bool AAetherCharacter::TrySkill(const FString& SkillId)
{
    if(!HasAuthority())
    {
        if(LocalCastInputSequence==MAX_uint32||SkillId.Len()>96||!SkillUnlocked(SkillId))return false;
        if(!CastExecutionId.IsValid()&&QueryAction(EAetherActionKind::Spell)!=EAetherActionDenial::None)return false;
        ++LocalCastInputSequence;LocalCastStarted=CombatTime();LocalCastUntil=LocalCastStarted+.35f;
        UE_LOG(LogTemp,Verbose,TEXT("AETHER_SPELL_INPUT input=%u skill=%s time=%.3f"),LocalCastInputSequence,*SkillId,LocalCastStarted);
        ServerRequestSkill(SkillId,LocalCastInputSequence,CastExecutionId);return true;
    }
    if(HasAuthority())BuffRuntime->FlushDue();
    if(!AbilitySystem||!SkillUnlocked(SkillId))return false;
    const auto* Spec=AetherSkillBinding::Find(*AbilitySystem,SkillId);
    if(Spec&&Spec->IsActive()&&CastExecutionId.IsValid()){AbilitySystem->CancelAbilityHandle(Spec->Handle);return true;}
    return Spec&&AbilitySystem->TryActivateAbility(Spec->Handle);
}
void AAetherCharacter::ServerRequestSkill_Implementation(const FString& Id,uint32 Sequence,FGuid CancelExecution)
{
    if(Sequence==0||Sequence<=LastServerCastInputSequence||Id.IsEmpty()||Id.Len()>96)return;
    LastServerCastInputSequence=Sequence;
    if(CancelExecution.IsValid())
    {
        if(CancelExecution==CastExecutionId&&AbilitySystem)
        {
            const auto* Spec=AetherSkillBinding::Find(*AbilitySystem,Id);
            if(Spec&&Spec->IsActive())AbilitySystem->CancelAbilityHandle(Spec->Handle);
        }
        ClientSkillFeedback(Sequence,false);return;
    }
    const bool Accepted=QueryAction(EAetherActionKind::Spell)==EAetherActionDenial::None&&TrySkill(Id);
    ClientSkillFeedback(Sequence,Accepted);
}
void AAetherCharacter::ClientSkillFeedback_Implementation(uint32 Sequence,bool Accepted)
{
    if(Sequence!=LocalCastInputSequence)return;
    if(!Accepted)LocalCastUntil=0;
    UE_LOG(LogTemp,Verbose,TEXT("AETHER_SPELL_ACK input=%u accepted=%d time=%.3f"),Sequence,Accepted,CombatTime());
}
float AAetherCharacter::SkillCooldownRemaining(const FString& Id) const
{
    const auto* PS=GetPlayerState<AAetherPlayerState>();const auto* Spec=AbilitySystem?AetherSkillBinding::Find(*AbilitySystem,Id):nullptr;
    const auto* E=FAetherSkillDefinitionsV10::Get().Effect(Id,Spec?Spec->Level:1);
    if(!E||!AbilitySystem||AbilitySystem->GetAvatarActor()!=this)return TNumericLimits<float>::Max();
    if(SkillAuthority==EAetherSkillAuthority::Profile)
    {
        if(!PS||PS->AbilitySystem!=AbilitySystem)return TNumericLimits<float>::Max();
        if(!HasAuthority()&&BuffRuntime->PresentationReady(PS->SkillGrants.ProfileRevision))
            return float(FMath::Max(0.,FMath::Max(BuffRuntime->Snapshot.Cooldowns.FindRef(TEXT("Skill.")+Id),BuffRuntime->Snapshot.Cooldowns.FindRef(TEXT("Group.")+E->CooldownGroup))-CombatTime()));
        return float(FMath::Min(PS->CooldownRemaining(Id,E->CooldownGroup,CombatTime()),double(TNumericLimits<float>::Max())));
    }
    const auto* DefinitionSystem=SkillAuthority==EAetherSkillAuthority::Definition&&!PS?Cast<UAetherDefinitionAbilitySystem>(AbilitySystem):nullptr;
    return DefinitionSystem?float(FMath::Min(DefinitionSystem->CooldownRemaining(Id,E->CooldownGroup,CombatTime()),double(TNumericLimits<float>::Max()))):TNumericLimits<float>::Max();
}
bool AAetherCharacter::TrySpell(int32 Spell)
{
    const FString Id=SkillAtInputSlot(Spell);return !Id.IsEmpty()&&TrySkill(Id);
}

bool AAetherCharacter::Ready() const
{return QueryAction(EAetherActionKind::General)==EAetherActionDenial::None;}
bool AAetherCharacter::ReadyIgnoringDodgeTag() const
{return QueryAction(EAetherActionKind::General,true)==EAetherActionDenial::None;}
EAetherActionDenial AAetherCharacter::QueryAction(EAetherActionKind Kind,bool IgnoreOwnedDodge) const
{
    FAetherActionContext C;const float T=CombatTime();const auto* Player=Cast<AAetherFrontierCharacter>(this);
    C.bAvatar=AbilitySystem&&AbilitySystem->GetAvatarActor()==this&&Equipment&&ResourceGate;
    C.bAlive=Alive();C.bStorage=!ResourceGate||ResourceGate->IsBlocked()||(BuffRuntime&&BuffRuntime->HasDue());
    C.bStunned=T<StunUntil||(BuffRuntime&&BuffRuntime->HasTag(TEXT("Stun")));
    C.bSilenced=BuffRuntime&&BuffRuntime->HasTag(TEXT("Silence"));C.bRecovery=T<ActionUntil||T<CastLockUntil;
    C.bBlocking=bBlocking;C.bBusy=!Equipment||Equipment->IsBusy()||(Player&&Player->WorldActions&&Player->WorldActions->IsBusy());
    C.bDodge=AbilitySystem&&AbilitySystem->HasMatchingGameplayTag(AetherDodge::ActiveTag());
    C.bVault=AbilitySystem&&AbilitySystem->HasMatchingGameplayTag(AetherVault::ActiveTag());C.bCarrying=Player&&Player->Carried;
    return AetherActionPolicy::Query(Kind,C,IgnoreOwnedDodge);
}
float AAetherCharacter::CombatTime() const
{ const auto* GS = GetWorld()->GetGameState(); return GS ? GS->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds(); }
void AAetherCharacter::SetVitals(float HP, float MP, float SP)
{
    if (!HasAuthority()||!AbilitySystem||AbilitySystem->GetAvatarActor()!=this) return;
    if(!FMath::IsFinite(HP)||!FMath::IsFinite(MP)||!FMath::IsFinite(SP))return;
    if(ResourceGate->IsBlocked())
    {
        TWeakObjectPtr<AAetherCharacter> Self=this;
        if(ResourceGate->Defer([Self,HP,MP,SP]{if(Self.IsValid())Self->SetVitals(HP,MP,SP);}))return;
    }
    AbilitySystem->SetNumericAttributeBase(UAetherAttributes::GetHealthAttribute(), FMath::Clamp(HP,0.f,MaxHealth));
    AbilitySystem->SetNumericAttributeBase(UAetherAttributes::GetManaAttribute(), FMath::Clamp(MP,0.f,MaximumMana()));
    AbilitySystem->SetNumericAttributeBase(UAetherAttributes::GetStaminaAttribute(), FMath::Clamp(SP,0.f,MaximumStamina()));
    if(!Alive())CancelActions();
}
void AAetherCharacter::ResetCombat()
{
    ActionUntil = CastLockUntil = StunUntil = CombatRuntime->InvulnerableUntil = 0; NextAI = 0; CombatRuntime->LastDamageAt = -100;
    bWindingUp = bBlocking = false; NextShockStun = 0;
    CancelActions();
    AbilitySystem->SetNumericAttributeBase(UAetherAttributes::GetPostureAttribute(), bUseBasicAssets ? 100 : 0);
    GetCharacterMovement()->StopMovementImmediately();
}
bool AAetherCharacter::FindSpellTarget(int32 Spell,FHitResult& Hit,FVector& Origin,FVector& Direction) const
{
    const FString Id=SkillAtInputSlot(Spell);
    const auto* Spec=AbilitySystem?AetherSkillBinding::Find(*AbilitySystem,Id):nullptr;
    return Spec&&FindSkillTarget(Id,Spec->Level,Hit,Origin,Direction);
}
bool AAetherCharacter::ExecuteSpell(int32 Spell)
{
    const FString Id=SkillAtInputSlot(Spell);
    const auto* Spec=AbilitySystem?AetherSkillBinding::Find(*AbilitySystem,Id):nullptr;
    return Spec&&ExecuteSkill(Id,Spec->Level);
}
bool AAetherCharacter::FindSkillTarget(const FString& SkillId,int32 Rank,FHitResult& Hit,FVector& Origin,FVector& Direction) const
{
    const auto& Definitions=FAetherSkillDefinitionsV10::Get();
    const auto* D=Definitions.Skills.Find(SkillId);const auto* E=Definitions.Effect(SkillId,Rank);
    if(!D||!E||!SkillUnlocked(SkillId))return false;
    Origin=GetActorLocation()+FVector(0,0,55);Direction=GetControlRotation().Vector();
    if(D->Mechanic==EAetherSkillMechanic::SelfBuff)return true;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(AetherSpell),false,this);
    GetWorld()->SweepSingleByChannel(Hit,Origin,Origin+Direction*E->RangeCm,FQuat::Identity,ECC_Visibility,FCollisionShape::MakeSphere(float(E->TargetRadiusCm)),Params);
    if(D->Mechanic==EAetherSkillMechanic::FriendlyTargetBuff)
    {
        auto* Target=Cast<AAetherCharacter>(Hit.GetActor());
        if(!Target||Target==this||Fighter!=EAetherFighter::Player||Target->Fighter!=EAetherFighter::Player||!Target->Alive())return false;
        if(HasAuthority()){Target->BuffRuntime->FlushDue();return !Target->ResourceGate->IsBlocked()&&Target->BuffRuntime->CanApply(E->BuffId);}
        return true;
    }
    if((D->Mechanic==EAetherSkillMechanic::Frost||D->Mechanic==EAetherSkillMechanic::Lightning)&&Fighter==EAetherFighter::Player)
        if(auto* Other=Cast<AAetherCharacter>(Hit.GetActor());Other&&Other->Fighter==EAetherFighter::Player)return false;
    return D->Mechanic==EAetherSkillMechanic::Fire||(Hit.GetActor()&&Hit.GetActor()->FindComponentByClass<UReactiveBodyComponent>());
}
bool AAetherCharacter::ExecuteSkill(const FString& SkillId,int32 Rank)
{
    const auto& Definitions=FAetherSkillDefinitionsV10::Get();
    const auto* D=Definitions.Skills.Find(SkillId);const auto* E=Definitions.Effect(SkillId,Rank);
    if(!D||!E)return false;
    FAetherCastExecution Cast;Cast.SkillId=SkillId;Cast.Rank=Rank;Cast.DefinitionRevision=Definitions.ContentSchemaVersion;
    Cast.Mechanic=D->Mechanic;Cast.Effect=*E;
    return ExecuteCast(Cast);
}
bool AAetherCharacter::ExecuteCast(const FAetherCastExecution& Cast)
{
    if(!HasAuthority()||QueryAction(EAetherActionKind::Spell)!=EAetherActionDenial::None||SkillCooldownRemaining(Cast.SkillId)>0)return false;
    // 执行前锁定唯一冷却所有者，效果回调不得把旧动作提交到替换 Pawn/ASC 的冷却表。
    const TWeakObjectPtr<AAetherPlayerState> ProfileCooldownOwner=SkillAuthority==EAetherSkillAuthority::Profile?GetPlayerState<AAetherPlayerState>():nullptr;
    const TWeakObjectPtr<UAetherDefinitionAbilitySystem> DefinitionCooldownOwner=SkillAuthority==EAetherSkillAuthority::Definition?::Cast<UAetherDefinitionAbilitySystem>(AbilitySystem):nullptr;
    if(!EnemySkillDecision.ValidateCommit(*this,Cast))return false;
    const auto* E=&Cast.Effect;
    FHitResult Hit;FVector Origin,Direction;
    if(!FindSkillTarget(Cast.SkillId,Cast.Rank,Hit,Origin,Direction))return false;
    bool Accepted=false;
    if(Cast.Mechanic==EAetherSkillMechanic::Fire)
    {
        const FTransform SpawnTransform(Direction.Rotation(),Origin);
        if(auto* Projectile=GetWorld()->SpawnActorDeferred<AAetherProjectile>(AAetherProjectile::StaticClass(),SpawnTransform,this,this,ESpawnActorCollisionHandlingMethod::AlwaysSpawn))
        {Projectile->VelocityCm=Direction*1300;Projectile->HeatJ=E->HeatJ;
         Projectile->MaxPathCm=E->RangeCm;Projectile->CollisionRadiusCm=float(E->TargetRadiusCm);
         Projectile->ExecutionId=Cast.ExecutionId;Projectile->FinishSpawning(SpawnTransform);Accepted=true;}
    }
    else if(Cast.Mechanic==EAetherSkillMechanic::SelfBuff)
    {
        FString Why;Accepted=BuffRuntime->Apply(E->BuffId,TEXT("Skill.")+Cast.SkillId,Why);Feedback=Why;
    }
    else if(Cast.Mechanic==EAetherSkillMechanic::FriendlyTargetBuff)
    {
        auto* Target=::Cast<AAetherCharacter>(Hit.GetActor());const auto* Receiver=Target?Target->ResourceGate->GetReceiver():nullptr;
        if(!Receiver||Receiver->State().LifeId!=Cast.TargetLifeId)return false;
        FString Why;Accepted=Target->BuffRuntime->Apply(E->BuffId,TEXT("Skill.")+Cast.SkillId+TEXT(".")+Cast.LifeId.ToString(EGuidFormats::Digits),Why);Feedback=Why;
    }
    else if(auto* Body=Hit.GetActor()?Hit.GetActor()->FindComponentByClass<UReactiveBodyComponent>():nullptr)
    {
        if(WaterReserveKg<E->WaterKg)return false;
        FReactiveStimulus S;S.SourceActor=this;S.WaterKg=E->WaterKg;S.HeatJ=E->HeatJ;S.ElectricalJ=E->ElectricalJ;
        Accepted=Body->Inject(S);
        // 只扣除模拟接受的水量。等级提升不能凭空造水，也不能在注入失败时丢失水。
        if(Accepted)WaterReserveKg-=float(E->WaterKg);
    }
    if(Accepted)
    {
        CastStartedAt=CombatTime();CastLockUntil=CastStartedAt+float(E->RecoverySeconds<0?E->Cooldown:E->RecoverySeconds);
        if(ProfileCooldownOwner.IsValid())ProfileCooldownOwner->CommitCooldown(Cast.SkillId,E->CooldownGroup,E->SkillCooldown,E->Cooldown,CastStartedAt);
        else if(DefinitionCooldownOwner.IsValid())DefinitionCooldownOwner->CommitCooldown(Cast.SkillId,E->CooldownGroup,E->SkillCooldown,E->Cooldown,CastStartedAt);
        EnemySkillDecision.RecordCommitted(*this,Cast);
    }
    return Accepted;
}

void AAetherCharacter::MoveForward(float V) { if (Alive()) AddMovementInput(FRotationMatrix(FRotator(0,GetControlRotation().Yaw,0)).GetUnitAxis(EAxis::X), V); }
void AAetherCharacter::MoveRight(float V) { if (Alive()) AddMovementInput(FRotationMatrix(FRotator(0,GetControlRotation().Yaw,0)).GetUnitAxis(EAxis::Y), V); }
void AAetherCharacter::SetupPlayerInputComponent(UInputComponent* I)
{
    Super::SetupPlayerInputComponent(I);
    I->BindAxis("AetherForward",this,&AAetherCharacter::MoveForward); I->BindAxis("AetherRight",this,&AAetherCharacter::MoveRight);
    I->BindAxisKey(EKeys::MouseX,this,&AAetherCharacter::Turn); I->BindAxisKey(EKeys::MouseY,this,&AAetherCharacter::Look);
    I->BindKey(EKeys::LeftMouseButton,IE_Pressed,this,&AAetherCharacter::Light);
    I->BindKey(EKeys::LeftShift,IE_Pressed,this,&AAetherCharacter::Heavy);
    I->BindKey(EKeys::RightMouseButton,IE_Pressed,this,&AAetherCharacter::BlockOn); I->BindKey(EKeys::RightMouseButton,IE_Released,this,&AAetherCharacter::BlockOff);
    I->BindKey(EKeys::SpaceBar,IE_Pressed,this,&AAetherCharacter::ServerDodge);
    I->BindKey(EKeys::F,IE_Pressed,this,&AAetherCharacter::CastSelected);
    I->BindKey(EKeys::One,IE_Pressed,this,&AAetherCharacter::SelectHeat); I->BindKey(EKeys::Two,IE_Pressed,this,&AAetherCharacter::SelectWater);
    I->BindKey(EKeys::Three,IE_Pressed,this,&AAetherCharacter::SelectFrost); I->BindKey(EKeys::Four,IE_Pressed,this,&AAetherCharacter::SelectLightning);
    I->BindKey(EKeys::E,IE_Pressed,this,&AAetherCharacter::Interact); I->BindKey(EKeys::Q,IE_Pressed,this,&AAetherCharacter::Alternate);
    I->BindKey(EKeys::F5,IE_Pressed,this,&AAetherCharacter::Save); I->BindKey(EKeys::F9,IE_Pressed,this,&AAetherCharacter::Load);
    I->BindKey(EKeys::R,IE_Pressed,this,&AAetherCharacter::CycleEquipment);
    I->BindKey(EKeys::T,IE_Pressed,this,&AAetherCharacter::ToggleShield);
    I->BindKey(EKeys::X,IE_Pressed,this,&AAetherCharacter::StowWeapon);
}
void AAetherCharacter::ServerAttack_Implementation(bool Heavy)
{
    PerformMelee(Heavy);
}
void AAetherCharacter::PerformMelee(bool Heavy)
{ RequestMelee(Heavy?TEXT("Heavy"):TEXT("Light")); }
bool AAetherCharacter::RequestMelee(FName Id)
{
    if(!HasAuthority()||!AbilitySystem||AbilitySystem->GetAvatarActor()!=this||(Id!=TEXT("Light")&&Id!=TEXT("Heavy")))return false;
    const uint64 Before=Equipment->AcceptedAttackCount;
    for(const auto& Spec:AbilitySystem->GetActivatableAbilities())
        if(Spec.Ability&&Spec.Ability->IsA<UAetherMeleeAbility>()&&Spec.Level==(Id==TEXT("Heavy")?2:1))
        {AbilitySystem->TryActivateAbility(Spec.Handle);break;}
    return Equipment->AcceptedAttackCount>Before;
}
void AAetherCharacter::ReceiveEquipmentHit_Implementation(const FAetherEquipmentHit& Hit)
{ if(!DeferEquipmentHit(Hit))ReceiveHit(Hit.Damage,Hit.PostureDamage,Cast<AAetherCharacter>(Hit.Source),true); }
void AAetherCharacter::ServerBlock_Implementation(bool Value)
{
    if (!Value) { bBlocking = false; return; }
    if (!Ready() || !Equipment->GuardDefinition()) return; bBlocking = true;
    const float T = CombatTime(); CombatRuntime->BlockStarted = T >= CombatRuntime->NextParryAllowed ? T : -100;
    if (CombatRuntime->BlockStarted > 0) CombatRuntime->NextParryAllowed = T + .7f;
}
bool AAetherCharacter::TryDodge()
{
    return AbilitySystem&&AbilitySystem->GetAvatarActor()==this&&
        AbilitySystem->TryActivateAbilityByClass(UAetherDodgeAbility::StaticClass());
}
void AAetherCharacter::RecordDodgeCommit()
{
    // 继续保留安全服务使用的最近战斗时间；实际成本与无敌窗口由能力效果负责。
    if(HasAuthority())if(const auto* Definition=AetherControlledActions::Find(TEXT("DodgeForward")))
        ActionUntil=CombatTime()+Definition->Duration;
}
void AAetherCharacter::ServerDodge_Implementation(){TryDodge();}
void AAetherCharacter::ReceiveHit(float Damage,float PostureDamage,AAetherCharacter* Source,bool CanBlock)
{CombatRuntime->ReceiveHit(Damage,PostureDamage,Source,CanBlock);}
void AAetherCharacter::ApplyPostureDamage(float Amount){CombatRuntime->ApplyPostureDamage(Amount);}
float AAetherCharacter::TakeDamage(float Amount,const FDamageEvent& Event,AController* EventInstigator,AActor* Causer)
{return CombatRuntime->ApplyDamage(Amount,Event,EventInstigator,Causer);}
void AAetherCharacter::Reaction(EReactiveReaction Kind, double Magnitude, FVector Vector)
{
    // Legacy reaction events are presentation-only. Shock gameplay uses the typed window below.
}
void AAetherCharacter::ElectricalWindow(const FReactiveElectricalWindow& Window)
{
    if(!HasAuthority()||!Alive()||Window.DurationSeconds<.001)return;
    BuffRuntime->FlushDue();
    const float EventTime=CombatTime();
    const double Wetness=Reactive->State.ElectricalWetness01;
    struct FExposureSnapshot
    {
        TWeakObjectPtr<AActor> Source;
        TWeakObjectPtr<AController> Controller;
        FAetherDefenseSnapshot Defense;
        float Damage=0;
    };
    TArray<FExposureSnapshot> Exposures;
    for(const auto& E:Window.Contributions)
    {
        AActor* Source=E.Source;auto* Pawn=Cast<APawn>(Source);
        FExposureSnapshot Snapshot;Snapshot.Source=Source;
        Snapshot.Controller=Pawn?Pawn->GetController():Source?Source->GetInstigatorController():nullptr;
        Snapshot.Defense=CombatRuntime->CaptureDefense(Source);Snapshot.Defense.Time=EventTime;
        Snapshot.Damage=float(E.DeliveredJ/140*(1+Wetness*.25));Exposures.Add(Snapshot);
    }
    const bool Strong=Window.DeliveredJ/Window.DurationSeconds>6000;
    const TWeakObjectPtr<AAetherCharacter> Self=this;
    // The same captured event is used immediately or after the resource barrier.
    // Never re-enter ElectricalWindow and resample later wetness/gear/time.
    auto Apply=[Self,Exposures=MoveTemp(Exposures),EventTime,Strong]()
    {
        if(!Self.IsValid()||!Self->HasAuthority())return;
        for(const auto& E:Exposures)
        {
            if(!Self->Alive())break;
            TGuardValue<TOptional<FAetherDefenseSnapshot>> Guard(Self->CombatRuntime->DeferredDefense,E.Defense);
            FDamageEvent Damage(UAetherStormDamage::StaticClass());
            Self->TakeDamage(E.Damage,Damage,E.Controller.Get(),E.Source.Get());
        }
        if(Strong&&EventTime>Self->NextShockStun&&Self->Alive())
        {
            Self->NextShockStun=EventTime+3;
            // Expired historical stuns must not cancel a newly started action.
            if(EventTime+.7f>Self->CombatTime())
            {
                Self->StunUntil=FMath::Max(Self->StunUntil,EventTime+.7f);
                Self->CancelActions();Self->bBlocking=false;
            }
        }
    };
    if(ResourceGate->IsBlocked())
    {
        ResourceGate->Defer(MoveTemp(Apply),EAetherEffectEventKind::Damage);return;
    }
    Apply();
}
void AAetherCharacter::Pacify() { if (HasAuthority()) { bPacified = true; CancelActions(); bBlocking = bWindingUp = false; GetCharacterMovement()->StopMovementImmediately(); } }
void AAetherCharacter::Think(float Dt)
{
    if (const auto* Mode = GetWorld()->GetAuthGameMode<AAetherAdventureMode>(); Mode && Mode->bSmoke) return;
    if (Fighter == EAetherFighter::Player || !Alive()) {EnemyPerception.Reset();return;}
    if(!FAetherNpcSkillDecision::IsControlled(*this)){EnemySkillDecision.Reset();EnemyPerception.Reset();bWindingUp=bBlocking=false;return;}
    const auto* Perception=FAetherNpcPerceptionDefinitions::Get().ForFighter(StaticEnum<EAetherFighter>()->GetNameStringByValue(int64(Fighter)));
    if(!Perception){EnemyPerception.Reset();bWindingUp=bBlocking=false;SteeringDirection=FVector::ZeroVector;return;}
    const float T = CombatTime();
    const auto Observed=EnemyPerception.Observe(*this,*Perception);
    AAetherCharacter* Target=Observed.Target.Get();
    if (T < StunUntil) { bWindingUp = false; bBlocking = false; return; }
    if (!Target) { bWindingUp = false;bBlocking=false;if(FVector::DistSquared2D(GetActorLocation(),Home)>FMath::Square(Perception->HomeArrivalRadiusCm)){const auto Dir=SafeMoveDirection(Home);SetActorRotation(Dir.Rotation());AddMovementInput(Dir,1);}return; }
    if(!Observed.bVisible){bWindingUp=false;bBlocking=false;AddMovementInput(SafeMoveDirection(Observed.LastSeenPosition),1);return;}
    // 移动/面向只消费本次可见观察；能力与真实近战命中仍在正式入口复验。
    FVector Toward = Observed.LastSeenPosition - GetActorLocation(); Toward.Z = 0;
    const auto* NpcLoadout=FAetherNpcSkillDefinitions::Get().Find(SkillLoadoutId);
    if(!NpcLoadout){bWindingUp=bBlocking=false;return;}
    if(!NpcLoadout->OffensiveSkills.IsEmpty())
    {
        SetActorRotation(Toward.Rotation());if(Controller)Controller->SetControlRotation(Toward.Rotation());
        const auto SkillChoice=EnemySkillDecision.Choose(*this,*Target);
        bWindingUp=bBlocking=false;
        if(SkillChoice.Kind==EAetherNpcSkillChoice::Ready)EnemySkillDecision.TryExecute(*this,*Target,SkillChoice.SkillId);
        else if(SkillChoice.Kind==EAetherNpcSkillChoice::Approach)AddMovementInput(SafeMoveDirection(Observed.LastSeenPosition),1);
        return;
    }
    if (bWindingUp)
    {
        if (T >= NextAI)
        {
            bWindingUp = false;
            if(Fighter==EAetherFighter::Wolf)LaunchCharacter(GetActorForwardVector()*450,false,false);PerformMelee(bAIHeavy);
            ActionUntil = T + (Fighter == EAetherFighter::BellKnight ? 1.25f : .8f); NextAI = ActionUntil;
        }
        return;
    }
    if (T < ActionUntil) return;
    SetActorRotation(Toward.Rotation()); if (Controller) Controller->SetControlRotation(Toward.Rotation());
    const auto* Main=Equipment->InSlot(TEXT("MainHand")); const auto* Move=Main?Main->FindAttack(TEXT("Light")):nullptr;
    const float Range = Move?Move->ReachCm-10:130;
    if (Toward.Size() > Range)
    { bBlocking = Fighter == EAetherFighter::ShieldGuard && Equipment->GuardDefinition(); AddMovementInput(SafeMoveDirection(Observed.LastSeenPosition), 1); }
    else if (T >= NextAI)
    {
        FCollisionQueryParams Params(SCENE_QUERY_STAT(AetherAI),false,this); Params.AddIgnoredActor(Target);
        if (GetWorld()->LineTraceTestByChannel(GetActorLocation(),Target->GetActorLocation(),ECC_Visibility,Params)) return;
        bBlocking = false; bWindingUp = true; bAIHeavy = Fighter == EAetherFighter::BellKnight || Fighter==EAetherFighter::Golem;
        NextAI = T + (Fighter == EAetherFighter::BellKnight && Health() < 160 ? .6f : .95f);
    }
}
void AAetherCharacter::Tick(float Dt)
{
    Super::Tick(Dt); const float T = CombatTime();
    if(bUseBasicAssets&&!Equipment->Catalog)if(auto* Assets=GetWorld()->GetSubsystem<UAetherAssetPreload>();Assets&&Assets->Ready()){
        auto* Content=Assets->Content.Get();Equipment->Catalog=Content->EquipmentCatalog;
        if(HasAuthority()&&!CharacterDefinition)CharacterDefinition=Fighter==EAetherFighter::Player?Content->Player:(Fighter==EAetherFighter::BellKnight||Fighter==EAetherFighter::Golem)?Content->Boss:Fighter==EAetherFighter::FireCaster?Content->Caster:Content->Guard;
        ApplyCharacterDefinition();if(HasAuthority()&&CharacterDefinition&&!Equipment->bProfileManaged)Equipment->RestoreLoadout(CharacterDefinition->InitialEquipment);
    }
    if(bUseBasicAssets&&CharacterDefinition&&!GetMesh()->GetSkeletalMeshAsset())ApplyCharacterDefinition();
    if (HasAuthority() && (!Alive() || T<StunUntil)) CancelActions();
    NetworkProbe(Dt);
    if (HasAuthority() && Alive() && AbilitySystem->GetAvatarActor()==this)
    {
        AdvanceCombatResources(Dt,Reactive->State.TemperatureC,Reactive->GetLastSourceActor());
        if(!ResourceGate->IsBlocked())Think(Dt);
    }
    GetCharacterMovement()->MaxWalkSpeed = !Alive() || T < StunUntil ? 0.f : (bBlocking ? 220.f : Fighter == EAetherFighter::Player ? 450.f : Fighter==EAetherFighter::Wolf?380.f:230.f) * (Reactive->State.IceFraction > .5 ? 1.f-.5f*AetherEquipmentMath::ElementMultiplier(Attributes->GearFrostResist.GetCurrentValue()) : 1.f);
    Tint(BodyVisual, !Alive() ? FLinearColor(.15f,.15f,.17f) : bWindingUp ? FLinearColor(1,.09f,.01f) : T < StunUntil ? FLinearColor(.1f,.8f,1) : Fighter == EAetherFighter::Player ? FLinearColor(.12f,.34f,.5f) : Fighter == EAetherFighter::FireCaster ? FLinearColor(.55f,.09f,.025f) : FLinearColor(.45f,.3f,.09f));
    if(Motion&&GetNetMode()!=NM_DedicatedServer)
    {
        const bool Controlled=AllowsGeneratedMotion();
        FName Style=bIsCrouched?(GetVelocity().Size2D()<5?FName("CrouchIdle"):FName("Crouch")):GetVelocity().Size2D()<5?FName("Idle"):Health()<MaxHealth*.3f?FName("Injured"):FName("Walk");
        if(!bIsCrouched&&Health()>=MaxHealth*.3f&&GetVelocity().Size2D()>=5)
            if(const auto* Player=Cast<AAetherFrontierCharacter>(this);Player&&Player->LockedTarget)
            {
                const float Side=FVector::DotProduct(GetVelocity().GetSafeNormal(),GetActorRightVector());
                Style=Side<-.35?FName("StrafeLeft"):Side>.35?FName("StrafeRight"):FName("Combat");
            }
        Motion->SetIntent(Controlled,Style,FGuid(0,0,uint32(Fighter),Equipment->Attack.Serial));
    }
    UpdateAnimation();
    const TCHAR* N = Fighter == EAetherFighter::BellKnight ? TEXT("OLEN / BELL KNIGHT") : Fighter == EAetherFighter::ShieldGuard ? TEXT("SHIELD GUARD") : Fighter == EAetherFighter::FireCaster ? TEXT("EMBER CASTER") : TEXT("OATHFARER");
    Nameplate->SetText(FText::FromString(FString::Printf(TEXT("%s  %.0f\n%s"), N, Health(), bPacified ? TEXT("OATH RELEASED") : bWindingUp ? TEXT("ATTACK INCOMING") : TEXT(""))));
    Nameplate->SetVisibility(Fighter != EAetherFighter::Player);
    if (auto* PC = GetWorld()->GetFirstPlayerController(); PC && PC->PlayerCameraManager) Nameplate->SetWorldRotation((PC->PlayerCameraManager->GetCameraLocation() - Nameplate->GetComponentLocation()).Rotation());
}
void AAetherCharacter::AdvanceCombatResources(float Dt,double Temperature,TWeakObjectPtr<AActor> HeatSource)
{
    if(!HasAuthority()||!Alive()||!AbilitySystem||AbilitySystem->GetAvatarActor()!=this||!FMath::IsFinite(Dt)||Dt<=0)return;
    BuffRuntime->FlushDue();
    const float SampleTime=CombatTime();
    const float SampleRegen=SampleTime>ActionUntil&&!bBlocking&&!Equipment->IsBusy()?20:4;
    const float PostureRate=bUseBasicAssets?12.f:-12.f;
    const double IntervalStart=ResourceAdvanceTimeline;ResourceAdvanceTimeline+=double(Dt);
    if(ResourceGate->IsBlocked())
    {
        const TWeakObjectPtr<AAetherCharacter> Self=this;
        {
            FAetherResourceAdvanceInterval I;I.End=ResourceAdvanceTimeline;I.Start=IntervalStart;
            I.StaminaRate=SampleRegen;I.PostureRate=PostureRate;
            I.HazardRate=Temperature>55?FMath::Min(25.0,(Temperature-55)*.12):0;
            I.bPreserveSteps=I.HazardRate>0;I.SourceIdentity=HeatSource.IsValid()?HeatSource->GetUniqueID():0;
            const auto Defense=CombatRuntime->CaptureDefense(HeatSource.Get());I.Defense=Defense.Fire;
            if(const auto* PS=GetPlayerState<AAetherPlayerState>())I.Identity.StateRevision=int64(PS->ProjectionRevision);
            if(const auto* Receiver=ResourceGate->GetReceiver())I.Identity.LifeId=Receiver->State().LifeId;
            if(ResourceGate->DeferInterval(I,[Self,SampleRegen,PostureRate,Hazard=I.HazardRate,Defense,HeatSource,LogicalStart=double(SampleTime-Dt),Elapsed=0.](double Duration) mutable {
                if(!Self.IsValid()||!Self->Alive()||Self->AbilitySystem->GetAvatarActor()!=Self.Get())return;
                Self->SetVitals(Self->Health(),Self->Mana()+float(Duration*5),Self->Stamina()+float(Duration*SampleRegen));
                const double Start=LogicalStart+Elapsed;Elapsed+=Duration;
                const double Eligible=FMath::Clamp(Start+Duration-FMath::Max(Start,double(Self->CombatRuntime->LastDamageAt)+2.),0.,Duration);
                Self->AbilitySystem->SetNumericAttributeBase(UAetherAttributes::GetPostureAttribute(),
                    FMath::Clamp(Self->Attributes->Posture.GetCurrentValue()+float(Eligible*PostureRate),0.f,100.f));
                if(Hazard>0){
                    auto StepDefense=Defense;StepDefense.Time=float(LogicalStart+Elapsed);
                    TGuardValue<TOptional<FAetherDefenseSnapshot>> Guard(Self->CombatRuntime->DeferredDefense,StepDefense);
                    FDamageEvent E(UAetherHeatExposureDamage::StaticClass());Self->TakeDamage(float(Hazard*Duration),E,nullptr,HeatSource.Get());
                }
            }))return;
        }
    }
    if(!HasAuthority()||!Alive()||!AbilitySystem||AbilitySystem->GetAvatarActor()!=this)return;
    const float T=CombatTime(),Regen=T>ActionUntil&&!bBlocking&&!Equipment->IsBusy()?20:4;
    // 延后的是增量恢复整段动作；不能排队旧绝对生命值而覆盖刚生效的药剂。
    SetVitals(Health(),Mana()+Dt*5,Stamina()+Dt*Regen);
    if(T-CombatRuntime->LastDamageAt>2)AbilitySystem->SetNumericAttributeBase(UAetherAttributes::GetPostureAttribute(),
        bUseBasicAssets?FMath::Min(100.f,Attributes->Posture.GetCurrentValue()+Dt*12):FMath::Max(0.f,Attributes->Posture.GetCurrentValue()-Dt*12));
    if(Temperature>55){FDamageEvent E(UAetherHeatExposureDamage::StaticClass());TakeDamage(float(FMath::Min(25.0,(Temperature-55)*.12)*Dt),E,nullptr,HeatSource.Get());}
}
bool AAetherCharacter::DeferEquipmentHit(const FAetherEquipmentHit& Hit)
{
    if(!ResourceGate->IsBlocked())return false;
    BuffRuntime->FlushDue();
    const TWeakObjectPtr<AAetherCharacter> Self=this;const TWeakObjectPtr<AActor> Source=Hit.Source;
    auto Copy=Hit;Copy.Source=nullptr;
    const auto Defense=CombatRuntime->CaptureDefense(Hit.Source);
    return ResourceGate->Defer([Self,Source,Copy,Defense]() mutable {
        if(!Self.IsValid())return;Copy.Source=Source.Get();
        TGuardValue<TOptional<FAetherDefenseSnapshot>> Guard(Self->CombatRuntime->DeferredDefense,Defense);
        Self->ReceiveEquipmentHit_Implementation(Copy);
    },EAetherEffectEventKind::Damage);
}
bool AAetherCharacter::DeferDamage(float Amount,const FDamageEvent& Event,AController* EventInstigator,AActor* Causer)
{
    if(!ResourceGate->IsBlocked())return false;
    BuffRuntime->FlushDue();
    const TWeakObjectPtr<AAetherCharacter> Self=this;const TWeakObjectPtr<AController> SourceController=EventInstigator;
    const TWeakObjectPtr<AActor> Source=Causer;const auto DamageClass=Event.DamageTypeClass;
    // 当前项目的伤害消费仅使用 DamageType 与标量 Amount；位置冲击在物理事件入口整体排队。
    const auto Defense=CombatRuntime->DeferredDefense.IsSet()?CombatRuntime->DeferredDefense.GetValue():CombatRuntime->CaptureDefense(Causer);
    return ResourceGate->Defer([Self,SourceController,Source,DamageClass,Amount,Defense]{
        if(Self.IsValid()){
            TGuardValue<TOptional<FAetherDefenseSnapshot>> Guard(Self->CombatRuntime->DeferredDefense,Defense);
            FDamageEvent Copy(DamageClass);Self->TakeDamage(Amount,Copy,SourceController.Get(),Source.Get());
        }
    },EAetherEffectEventKind::Damage);
}
void AAetherCharacter::NetworkProbe(float Dt)
{
    if (!FParse::Param(FCommandLine::Get(),TEXT("AetherNetClient")) || !IsLocallyControlled() || HasAuthority() || bNetProbeFinished) return;
    NetProbeTime += Dt;
    auto* GS = GetWorld()->GetGameState<AAetherAdventureState>();
    bool FrozenBaseline = false;
    for (TActorIterator<AAetherWorldObject> It(GetWorld()); It; ++It)
        if (It->Spec.Id == TEXT("IcePath0") && It->Reactive->State.IceFraction >= .95
            && It->Mesh->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block) FrozenBaseline = true;
    if (GS && GS->Quest.bRecord && FrozenBaseline && !bNetCastRequested && AbilitySystem->GetActivatableAbilities().Num() == 4)
    { bNetCastRequested = TrySpell(0); UE_LOG(LogTemp,Display,TEXT("AETHER_NET_CAST_REQUEST %d"),bNetCastRequested); }
    bool RemoteEquipment=false;
    for (TActorIterator<AAetherCharacter> It(GetWorld()); It; ++It)
        if (It->Fighter==EAetherFighter::ShieldGuard)
            if (const auto* Item=It->Equipment->InSlot(TEXT("MainHand")))
                RemoteEquipment=Item->ItemId==TEXT("TrainingHammer") && It->Equipment->VisualForSlot(TEXT("MainHand")) && !It->Equipment->InSlot(TEXT("OffHand"));
    if (bNetCastRequested && Mana()<95) bNetCostObserved=true;
    if (bNetCostObserved && !bNetEquipRequested && CombatTime()>CastLockUntil+.15f)
    { Equipment->Equip(TEXT("TrainingHammer")); bNetEquipRequested=true; }
    const auto* LocalItem=Equipment->InSlot(TEXT("MainHand"));
    const bool LocalEquipment=LocalItem && LocalItem->ItemId==TEXT("TrainingHammer") && Equipment->VisualForSlot(TEXT("MainHand")) && !Equipment->InSlot(TEXT("OffHand"));
    if (bNetCostObserved && LocalEquipment && RemoteEquipment && FrozenBaseline && GS && GS->Quest.bRecord)
    {
        FReactiveStimulus Forbidden; Forbidden.HeatJ = 50000;
        const bool NoAuthority = !GetWorld()->GetSubsystem<UReactiveWorldSubsystem>()->Submit(nullptr,Forbidden);
        const bool NoLocalSolver = GetWorld()->GetSubsystem<UReactiveWorldSubsystem>()->GetSimulation()->GetStats().Registered == 0;
        bNetProbeFinished = true;
        UE_LOG(LogTemp,Display,TEXT("AETHER_NET_CLIENT_%s mana=%.1f frozen=%d authority_guard=%d local_solver_empty=%d equipment_rpc=%d remote_equipment=%d"),NoAuthority&&NoLocalSolver?TEXT("PASS"):TEXT("FAIL"),Mana(),FrozenBaseline,NoAuthority,NoLocalSolver,LocalEquipment,RemoteEquipment);
        FPlatformMisc::RequestExitWithStatus(false,NoAuthority&&NoLocalSolver?0:1);
    }
    if (NetProbeTime > 35)
    { bNetProbeFinished = true; UE_LOG(LogTemp,Display,TEXT("AETHER_NET_CLIENT_FAIL timeout frozen=%d abilities=%d quest=%d cost=%d local_equipment=%d remote_equipment=%d"),FrozenBaseline,AbilitySystem->GetActivatableAbilities().Num(),GS&&GS->Quest.bRecord,bNetCostObserved,LocalEquipment,RemoteEquipment); FPlatformMisc::RequestExitWithStatus(false,1); }
}
void AAetherCharacter::ClientFeedback_Implementation(const FString& Message) { Feedback = Message; }
void AAetherCharacter::ServerInteract_Implementation(bool Alternate)
{ if (Alive()) if (auto* Mode = GetWorld()->GetAuthGameMode<AAetherAdventureMode>()) ClientFeedback(Mode->Interact(this, Alternate)); }
void AAetherCharacter::ServerSave_Implementation(bool Load)
{ if (auto* Mode = GetWorld()->GetAuthGameMode<AAetherAdventureMode>()) ClientFeedback(Load ? Mode->LoadAdventure(this) : Mode->SaveAdventure(this)); }

FVector AAetherCharacter::SafeMoveDirection(FVector Destination)
{
    auto* FloorActor=GetCharacterMovement()->CurrentFloor.HitResult.GetActor();
    auto* EscapeIce=IsValid(FloorActor)?FloorActor->FindComponentByClass<UReactiveBodyComponent>():nullptr;
    const auto GroundAllowed=[&](const FHitResult& Floor)
    {
        auto* Actor=Floor.GetActor();if(!IsValid(Actor))return false;
        if(auto* Bridge=Actor->FindComponentByClass<UAetherTraversalComponent>();Bridge&&Bridge->bAuthoredBridge&&!Bridge->bRouteOpen){NextPathAt=0;return false;}
        if(auto* Ice=Actor->FindComponentByClass<UReactiveBodyComponent>();Ice&&Ice->bIceControlsPawnCollision&&Ice->IceSupport!=EReactiveIceSupport::Bearing&&!(Ice==EscapeIce&&Ice->IceSupport==EReactiveIceSupport::Thawing)){NextPathAt=0;return false;}
        return true;
    };
    if(CombatTime()<NextSteeringAt)
    {
        FHitResult Floor;
        if(SteeringDirection.IsNearlyZero()||(AetherNavigationProbe::IsLocalStepClear(*this,SteeringDirection*150,Floor)&&GroundAllowed(Floor)))return SteeringDirection;
        SteeringDirection=FVector::ZeroVector;NavigationPoints.Reset();NextPathAt=0;
    }
    NextSteeringAt=CombatTime()+.2f;
    // Recast paths are bounded to the locally generated tiles. Missing paths fall back to guarded steering.
    if(CombatTime()>=NextPathAt||FVector::DistSquared2D(Destination,NavigationGoal)>FMath::Square(200.))
    {
        NextPathAt=CombatTime()+.75f;NavigationGoal=Destination;NavigationPoints.Reset();NavigationIndex=0;
        if(auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
        {FNavLocation End;const FVector LocalGoal=GetActorLocation()+(Destination-GetActorLocation()).GetClampedToMaxSize2D(2100);
         if(Nav->ProjectPointToNavigation(LocalGoal,End,FVector(220,220,350)))
          if(auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(),GetActorLocation(),End.Location,this);Path&&Path->IsValid())NavigationPoints=Path->PathPoints;}
    }
    while(NavigationPoints.IsValidIndex(NavigationIndex)&&FVector::DistSquared2D(GetActorLocation(),NavigationPoints[NavigationIndex])<FMath::Square(90.))++NavigationIndex;
    FVector Waypoint=NavigationPoints.IsValidIndex(NavigationIndex)?NavigationPoints[NavigationIndex]:Destination;
    if(IsValid(EscapeIce)&&EscapeIce->bIceControlsPawnCollision&&EscapeIce->IceSupport==EReactiveIceSupport::Thawing&&IsValid(EscapeIce->GetPrimitive()))
    {
        const FBox Box=EscapeIce->GetPrimitive()->Bounds.GetBox();const FVector Here=GetActorLocation();
        TArray<FVector> Exits={FVector(Box.Min.X-100,Here.Y,Here.Z),FVector(Box.Max.X+100,Here.Y,Here.Z),FVector(Here.X,Box.Min.Y-100,Here.Z),FVector(Here.X,Box.Max.Y+100,Here.Z)};
        Exits.Sort([&](const FVector& A,const FVector& B){return FVector::DistSquared2D(A,Here)<FVector::DistSquared2D(B,Here);});
        Waypoint=Exits[0];NextPathAt=0;NavigationPoints.Reset();
    }
    FVector Desired=(Waypoint-GetActorLocation()).GetSafeNormal2D();double Best=-1.e30;SteeringDirection=FVector::ZeroVector;
    auto* World=GetWorld()->GetSubsystem<UReactiveWorldSubsystem>();const auto* Sim=World?World->GetSimulation():nullptr;
    for(float Angle:{0.f,45.f,-45.f,90.f,-90.f,135.f,-135.f})
    {
        FVector Dir=Desired.RotateAngleAxis(Angle,FVector::UpVector);FVector End=GetActorLocation()+Dir*150;
        FHitResult Floor;if(!AetherNavigationProbe::IsLocalStepClear(*this,Dir*150,Floor)||!GroundAllowed(Floor))continue;
        double Score=FVector::DotProduct(Dir,Desired);if(Sim)for(auto Id:Sim->Query(End,70))
        {const auto* State=Sim->Find(Id);if(State&&(State->bBurning||State->TemperatureC>100))Score-=5;}
        if(Score>Best){Best=Score;SteeringDirection=Dir;}
    }
    if(SteeringDirection.IsNearlyZero())NextPathAt=0;
    return SteeringDirection;
}

bool AAetherCharacter::AllowsGeneratedMotion() const
{
    const float T=CombatTime();
    return Alive()&&T>=StunUntil&&T>=CastLockUntil&&T>=ActionUntil&&!Equipment->IsBusy()&&!bBlocking&&!AbilitySystem->HasMatchingGameplayTag(AetherDodge::ActiveTag())&&!AbilitySystem->HasMatchingGameplayTag(AetherVault::ActiveTag())&&
           !GetCharacterMovement()->IsFalling()&&!ResourceGate->IsBlocked();
}
