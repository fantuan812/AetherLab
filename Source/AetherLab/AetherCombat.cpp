#include "AetherCombat.h"
#include "AetherAdventure.h"
#include "AetherFrontier.h"
#include "AetherActions.h"
#include "AetherTraversal.h"
#include "AetherAnimation.h"
#include "AetherContent.h"
#include "AetherAssetPreload.h"
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
UAetherAttributes::UAetherAttributes()
{ Health.SetBaseValue(100); Health.SetCurrentValue(100); Mana.SetBaseValue(100); Mana.SetCurrentValue(100);
  Stamina.SetBaseValue(100); Stamina.SetCurrentValue(100); Posture.SetBaseValue(0); Posture.SetCurrentValue(0); }
void UAetherAttributes::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME_CONDITION_NOTIFY(UAetherAttributes, Health, COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UAetherAttributes, Mana, COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UAetherAttributes, Stamina, COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UAetherAttributes, Posture, COND_None, REPNOTIFY_Always);
}
void UAetherAttributes::OnRep_Health(const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(UAetherAttributes, Health, Old); }
void UAetherAttributes::OnRep_Mana(const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(UAetherAttributes, Mana, Old); }
void UAetherAttributes::OnRep_Stamina(const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(UAetherAttributes, Stamina, Old); }
void UAetherAttributes::OnRep_Posture(const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(UAetherAttributes, Posture, Old); }

UAetherSpellAbility::UAetherSpellAbility()
{ InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor; NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly; }
float UAetherSpellAbility::Cost(int32 Spell) { const float Costs[] = {18, 12, 20, 25}; return Costs[FMath::Clamp(Spell, 0, 3)]; }
bool UAetherSpellAbility::CheckCost(FGameplayAbilitySpecHandle H, const FGameplayAbilityActorInfo* Info, FGameplayTagContainer* Tags) const
{
    const auto* C = Info ? Cast<AAetherCharacter>(Info->AvatarActor.Get()) : nullptr;
    const int32 Spell = GetAbilityLevel(H, Info) - 1;
    return C && C->Ready() && C->SpellUnlocked(Spell) && Spell >= 0 && Spell < 4 && C->Mana() >= Cost(Spell)
        && (Spell != 1 || C->WaterReserveKg >= 0.5f) && Super::CheckCost(H, Info, Tags);
}
void UAetherSpellAbility::ApplyCost(FGameplayAbilitySpecHandle H, const FGameplayAbilityActorInfo* Info, FGameplayAbilityActivationInfo A) const
{
    Info->AbilitySystemComponent->ApplyModToAttribute(UAetherAttributes::GetManaAttribute(), EGameplayModOp::Additive, -Cost(GetAbilityLevel(H, Info) - 1));
}
void UAetherSpellAbility::ActivateAbility(FGameplayAbilitySpecHandle H, const FGameplayAbilityActorInfo* Info, FGameplayAbilityActivationInfo A, const FGameplayEventData* Event)
{
    auto* C = Cast<AAetherCharacter>(Info->AvatarActor.Get()); const int32 Spell = GetAbilityLevel(H, Info) - 1;
    FHitResult Hit; FVector Origin, Direction;
    if (!C || !C->HasAuthority() || !C->FindSpellTarget(Spell, Hit, Origin, Direction) || !CommitAbility(H, Info, A))
    { EndAbility(H, Info, A, true, true); return; }
    const bool Executed = C->ExecuteSpell(Spell);
    if (!Executed) C->AbilitySystem->ApplyModToAttribute(UAetherAttributes::GetManaAttribute(), EGameplayModOp::Additive, Cost(Spell));
    EndAbility(H, Info, A, true, !Executed);
}

AAetherCharacter::AAetherCharacter()
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
    Equipment = CreateDefaultSubobject<UAetherEquipmentComponent>(TEXT("Equipment"));
    GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Nameplate = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Nameplate")); Nameplate->SetupAttachment(RootComponent);
    Nameplate->SetRelativeLocation(FVector(0,0,120)); Nameplate->SetHorizontalAlignment(EHTA_Center); Nameplate->SetWorldSize(22); Nameplate->SetTextRenderColor(FColor::White);
    AbilitySystem = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("Abilities")); AbilitySystem->SetIsReplicated(true);
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
        GrantSpells(); SetVitals(MaxHealth, 100, 100);
        if (Fighter != EAetherFighter::Player && !Controller) SpawnDefaultController();
    }
    if (IsLocallyControlled() && Fighter == EAetherFighter::Player)
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
        if(bUseBasicAssets&&GetNetMode()!=NM_DedicatedServer){GetMesh()->SetAnimationMode(EAnimationMode::AnimationBlueprint);GetMesh()->SetAnimInstanceClass(UAetherAnimInstance::StaticClass());}
        Equipment->SetAttachmentTarget(GetMesh());
        Nameplate->SetRelativeLocation(FVector(0,0,CharacterDefinition->CapsuleHalfHeight+35));
    }
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
    DOREPLIFETIME(AAetherCharacter, MaxHealth); DOREPLIFETIME(AAetherCharacter, Fighter); DOREPLIFETIME(AAetherCharacter, bBlocking);
    DOREPLIFETIME(AAetherCharacter, bWindingUp); DOREPLIFETIME(AAetherCharacter, bPacified);
    DOREPLIFETIME(AAetherCharacter, WaterReserveKg); DOREPLIFETIME(AAetherCharacter, CastLockUntil); DOREPLIFETIME(AAetherCharacter, StunUntil);
    DOREPLIFETIME(AAetherCharacter, CharacterDefinition); DOREPLIFETIME(AAetherCharacter,bUseBasicAssets);
}
void AAetherCharacter::PossessedBy(AController* C)
{ CancelActions(); Super::PossessedBy(C); AbilitySystem->InitAbilityActorInfo(AbilitySystem->GetOwner(),this); }
void AAetherCharacter::CancelActions()
{
    if(!HasAuthority())return;
    Equipment->CancelAttack();
    // A PlayerState ASC may already have moved to a replacement pawn.
    if(AbilitySystem&&AbilitySystem->GetAvatarActor()==this)AbilitySystem->CancelAllAbilities();
}
void AAetherCharacter::UnPossessed()
{
    CancelActions();
    if(AbilitySystem&&AbilitySystem->GetAvatarActor()==this)AbilitySystem->ClearActorInfo();
    Super::UnPossessed();
}
void AAetherCharacter::EndPlay(const EEndPlayReason::Type Reason)
{
    CancelActions();
    if(AbilitySystem&&AbilitySystem->GetAvatarActor()==this)AbilitySystem->ClearActorInfo();
    Super::EndPlay(Reason);
}
void AAetherCharacter::OnRep_Controller()
{ Super::OnRep_Controller(); AbilitySystem->InitAbilityActorInfo(AbilitySystem->GetOwner(),this); }
void AAetherCharacter::GrantSpells()
{
    if(HasAuthority())
    {
        if(!AbilitySystem->FindAbilitySpecFromClass(UAetherReviveAbility::StaticClass()))AbilitySystem->GiveAbility(FGameplayAbilitySpec(UAetherReviveAbility::StaticClass(),1,8,this));
        for(int32 Level=1;Level<=2;++Level)
        {bool Found=false;for(const auto& Spec:AbilitySystem->GetActivatableAbilities())if(Spec.Ability&&Spec.Ability->IsA<UAetherMeleeAbility>()&&Spec.Level==Level)Found=true;
        if(!Found)AbilitySystem->GiveAbility(FGameplayAbilitySpec(UAetherMeleeAbility::StaticClass(),Level,4+Level,this));}
    }
    if (!HasAuthority()) return;
    SpellHandles.Reset();
    for (int32 I=0;I<4;++I)
    {
        bool Exists=false;
        for (const FGameplayAbilitySpec& Spec:AbilitySystem->GetActivatableAbilities())
            if (Spec.Ability && Spec.Ability->GetClass()==UAetherSpellAbility::StaticClass() && Spec.InputID==I)
            { SpellHandles.Add(Spec.Handle);Exists=true;break; }
        if (!Exists) SpellHandles.Add(AbilitySystem->GiveAbility(FGameplayAbilitySpec(UAetherSpellAbility::StaticClass(),I+1,I)));
    }
}
bool AAetherCharacter::TrySpell(int32 Spell)
{
    if (Spell < 0 || Spell > 3 || !SpellUnlocked(Spell)) return false;
    for (const FGameplayAbilitySpec& Spec : AbilitySystem->GetActivatableAbilities())
        if (Spec.InputID == Spell) return AbilitySystem->TryActivateAbility(Spec.Handle);
    return false;
}
bool AAetherCharacter::Ready() const
{ const float T = CombatTime(); return AbilitySystem && AbilitySystem->GetAvatarActor()==this && Alive() && T >= ActionUntil && T >= CastLockUntil && T >= StunUntil && !bBlocking && !Equipment->IsBusy(); }
float AAetherCharacter::CombatTime() const
{ const auto* GS = GetWorld()->GetGameState(); return GS ? GS->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds(); }
void AAetherCharacter::SetVitals(float HP, float MP, float SP)
{
    if (!HasAuthority()||!AbilitySystem||AbilitySystem->GetAvatarActor()!=this) return;
    AbilitySystem->SetNumericAttributeBase(UAetherAttributes::GetHealthAttribute(), FMath::Clamp(HP,0.f,MaxHealth));
    AbilitySystem->SetNumericAttributeBase(UAetherAttributes::GetManaAttribute(), FMath::Clamp(MP,0.f,100.f));
    AbilitySystem->SetNumericAttributeBase(UAetherAttributes::GetStaminaAttribute(), FMath::Clamp(SP,0.f,100.f));
    if(!Alive())CancelActions();
}
void AAetherCharacter::ResetCombat()
{
    ActionUntil = CastLockUntil = StunUntil = InvulnerableUntil = 0; NextAI = 0; LastDamageAt = -100;
    bWindingUp = bBlocking = false; NextShockStun = 0;
    CancelActions();
    AbilitySystem->SetNumericAttributeBase(UAetherAttributes::GetPostureAttribute(), bUseBasicAssets ? 100 : 0);
    GetCharacterMovement()->StopMovementImmediately();
}
bool AAetherCharacter::FindSpellTarget(int32 Spell, FHitResult& Hit, FVector& Origin, FVector& Direction) const
{
    if (Spell < 0 || Spell > 3 || !SpellUnlocked(Spell)) return false;
    Origin = GetActorLocation() + FVector(0,0,55); Direction = GetControlRotation().Vector();
    FCollisionQueryParams Params(SCENE_QUERY_STAT(AetherSpell), false, this);
    GetWorld()->SweepSingleByChannel(Hit, Origin, Origin + Direction * 1800, FQuat::Identity, ECC_Visibility, FCollisionShape::MakeSphere(12), Params);
    if(Spell>=2&&Fighter==EAetherFighter::Player)if(auto* Other=Cast<AAetherCharacter>(Hit.GetActor());Other&&Other->Fighter==EAetherFighter::Player)return false;
    return Spell == 0 || (Hit.GetActor() && Hit.GetActor()->FindComponentByClass<UReactiveBodyComponent>());
}
bool AAetherCharacter::ExecuteSpell(int32 Spell)
{
    if (!HasAuthority() || !Alive()) return false;
    FHitResult Hit; FVector Origin, Direction; if (!FindSpellTarget(Spell, Hit, Origin, Direction)) return false;
    bool Accepted = false;
    if (Spell == 0)
    {
        FActorSpawnParameters P; P.Owner = this; P.Instigator = this; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        if (auto* Projectile = GetWorld()->SpawnActor<AAetherProjectile>(Origin, Direction.Rotation(), P))
        { Projectile->VelocityCm = Direction * 1300; Accepted = true; }
    }
    else if (auto* Body = Hit.GetActor()->FindComponentByClass<UReactiveBodyComponent>())
    {
        FReactiveStimulus S; S.SourceActor = this;
        if (Spell == 1) { if (WaterReserveKg < .5f) return false; S.WaterKg = .5; }
        if (Spell == 2) S.HeatJ = -250000;
        if (Spell == 3) S.ElectricalJ = 6000;
        Accepted = Body->Inject(S);
        if (Accepted && Spell == 1) WaterReserveKg -= .5f;
    }
    if (Accepted) CastLockUntil = CombatTime() + (Spell == 2 ? .8f : .65f);
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
{ ReceiveHit(Hit.Damage,Hit.PostureDamage,Cast<AAetherCharacter>(Hit.Source),true); }
void AAetherCharacter::ServerBlock_Implementation(bool Value)
{
    if (!Value) { bBlocking = false; return; }
    if (!Ready() || !Equipment->GuardDefinition()) return; bBlocking = true;
    const float T = CombatTime(); BlockStarted = T >= NextParryAllowed ? T : -100;
    if (BlockStarted > 0) NextParryAllowed = T + .7f;
}
void AAetherCharacter::ServerDodge_Implementation()
{
    if (!Ready() || Stamina() < 18) return;
    AbilitySystem->ApplyModToAttribute(UAetherAttributes::GetStaminaAttribute(), EGameplayModOp::Additive, -18);
    const float T = CombatTime(); ActionUntil = T + .55f; InvulnerableUntil = T + .22f;
    const FVector Direction = GetLastMovementInputVector().IsNearlyZero() ? GetActorForwardVector() : GetLastMovementInputVector().GetSafeNormal();
    LaunchCharacter(Direction * 700 + FVector(0,0,80), true, false);
}
void AAetherCharacter::ReceiveHit(float Damage, float PostureDamage, AAetherCharacter* Source, bool CanBlock)
{
    if (!HasAuthority() || !Alive() || CombatTime() < InvulnerableUntil) return;
    const float T = CombatTime();
    const bool Front = Source && FVector::DotProduct(GetActorForwardVector(), (Source->GetActorLocation() - GetActorLocation()).GetSafeNormal()) > .25;
    const auto* Guard=Equipment->GuardDefinition();
    if (CanBlock && bBlocking && Front && Guard)
    {
        if (T - BlockStarted < Guard->ParryWindowSeconds) { if (Source) { Source->StunUntil = T + 1.1f; Source->CancelActions(); } return; }
        const float Cost = PostureDamage * Guard->GuardStaminaMultiplier;
        if (Stamina() >= Cost) { AbilitySystem->ApplyModToAttribute(UAetherAttributes::GetStaminaAttribute(), EGameplayModOp::Additive, -Cost); return; }
        bBlocking = false; StunUntil = T + 1.2f; CancelActions();
    }
    ApplyPostureDamage(PostureDamage);
    FDamageEvent Event; TakeDamage(Damage, Event, Source ? Source->GetController() : nullptr, Source);
}
void AAetherCharacter::ApplyPostureDamage(float Amount)
{
    if(!HasAuthority()||!AbilitySystem||AbilitySystem->GetAvatarActor()!=this||!Alive()||!FMath::IsFinite(Amount)||Amount<=0)return;
    float Posture=bUseBasicAssets?Attributes->Posture.GetCurrentValue()-Amount:Attributes->Posture.GetCurrentValue()+Amount;
    if(bUseBasicAssets?Posture<=0:Posture>=100)
    {StunUntil=CombatTime()+1.5f;Posture=0;bBlocking=false;CancelActions();}
    AbilitySystem->SetNumericAttributeBase(UAetherAttributes::GetPostureAttribute(),Posture);
}
float AAetherCharacter::TakeDamage(float Amount, const FDamageEvent& Event, AController* EventInstigator, AActor* Causer)
{
    if (!HasAuthority() || !AbilitySystem || AbilitySystem->GetAvatarActor()!=this || !Alive() || !FMath::IsFinite(Amount) || Amount <= 0) return 0;
    const float Applied = FMath::Min(Health(), Amount);
    AbilitySystem->ApplyModToAttribute(UAetherAttributes::GetHealthAttribute(), EGameplayModOp::Additive, -Applied);
    LastDamager = Causer; ++DamageReceivedCount; LastDamageAt = CombatTime();
    if (!Alive()) { CancelActions(); bBlocking = bWindingUp = false; GetCharacterMovement()->StopMovementImmediately(); }
    return Applied;
}
void AAetherCharacter::Reaction(EReactiveReaction Kind, double Magnitude, FVector Vector)
{
    // Legacy reaction events are presentation-only. Shock gameplay uses the typed window below.
}
void AAetherCharacter::ElectricalWindow(const FReactiveElectricalWindow& Window)
{
    if(!HasAuthority()||!Alive()||Window.DurationSeconds<.001)return;
    for(const auto& Exposure:Window.Contributions)
    {
        if(!Alive())break;
        FDamageEvent Event;AActor* Source=Exposure.Source;
        auto* Pawn=Cast<APawn>(Source);auto* SourceController=Pawn?Pawn->GetController():Source?Source->GetInstigatorController():nullptr;
        TakeDamage(float(Exposure.DeliveredJ/140*(1+Reactive->State.ElectricalWetness01*.25)),Event,SourceController,Source);
    }
    const float T=CombatTime();
    // 300 J in the old 50 ms window is 6000 W. Sum all sources before comparing exposure power.
    if(Window.DeliveredJ/Window.DurationSeconds>6000&&T>NextShockStun&&Alive())
    {StunUntil=T+.7f;CancelActions();NextShockStun=T+3;bBlocking=false;}
}
void AAetherCharacter::Pacify() { if (HasAuthority()) { bPacified = true; CancelActions(); bBlocking = bWindingUp = false; GetCharacterMovement()->StopMovementImmediately(); } }
void AAetherCharacter::Think(float Dt)
{
    if (const auto* Mode = GetWorld()->GetAuthGameMode<AAetherAdventureMode>(); Mode && Mode->bSmoke) return;
    if (Fighter == EAetherFighter::Player || !Alive()) return;
    const float T = CombatTime();
    if(T>=NextPerceptionAt)
    {
    NextPerceptionAt=T+.25f;
    AAetherCharacter* Target = nullptr; double Best = FMath::Square(1300.0);
    for (TActorIterator<AAetherCharacter> It(GetWorld()); It; ++It)
        if (It->Fighter == EAetherFighter::Player && It->Alive()&&(!Cast<AAetherCharacter>(GetOwner())||GetOwner()==*It))
        { if(const auto* FC=Cast<AAetherFrontierCharacter>(this);FC&&!FC->EncounterId.IsNone())
            if(auto* M=GetWorld()->GetAuthGameMode<AAetherFrontierMode>();M&&M->Encounters&&!M->Encounters->Participates(Cast<AAetherFrontierCharacter>(*It),FC->EncounterId))continue;
          const double D = FVector::DistSquared(It->GetActorLocation(), GetActorLocation());
          if(D<Best&&FVector::DistSquared(It->GetActorLocation(),Home)<FMath::Square(2600.))
          {FCollisionQueryParams Q(SCENE_QUERY_STAT(AetherPerception),false,this);Q.AddIgnoredActor(*It);
           if(!GetWorld()->LineTraceTestByChannel(GetActorLocation()+FVector(0,0,40),It->GetActorLocation()+FVector(0,0,40),ECC_Visibility,Q)){Target=*It;Best=D;}} }
    if(Target){PerceivedTarget=Target;LastSeenAt=T;LastSeenPosition=Target->GetActorLocation();}
    }
    AAetherCharacter* Target=PerceivedTarget.Get();
    if(Target&&(!Target->Alive()||T-LastSeenAt>4||FVector::DistSquared(GetActorLocation(),Home)>FMath::Square(2800.))){PerceivedTarget.Reset();Target=nullptr;}
    if (T < StunUntil) { bWindingUp = false; bBlocking = false; return; }
    if (!Target) { bWindingUp = false;bBlocking=false;if(FVector::DistSquared2D(GetActorLocation(),Home)>FMath::Square(120.)){const auto Dir=SafeMoveDirection(Home);SetActorRotation(Dir.Rotation());AddMovementInput(Dir,1);}return; }
    if(T-LastSeenAt>.3f){bWindingUp=false;bBlocking=false;AddMovementInput(SafeMoveDirection(LastSeenPosition),1);return;}
    FVector Toward = Target->GetActorLocation() - GetActorLocation(); Toward.Z = 0;
    if (bWindingUp)
    {
        if (T >= NextAI)
        {
            bWindingUp = false;
            if (Fighter == EAetherFighter::FireCaster) TrySpell(0); else {if(Fighter==EAetherFighter::Wolf)LaunchCharacter(GetActorForwardVector()*450,false,false);PerformMelee(bAIHeavy);}
            ActionUntil = T + (Fighter == EAetherFighter::BellKnight ? 1.25f : .8f); NextAI = ActionUntil;
        }
        return;
    }
    if (T < ActionUntil) return;
    SetActorRotation(Toward.Rotation()); if (Controller) Controller->SetControlRotation(Toward.Rotation());
    const auto* Main=Equipment->InSlot(TEXT("MainHand")); const auto* Move=Main?Main->FindAttack(TEXT("Light")):nullptr;
    const float Range = Fighter == EAetherFighter::FireCaster ? 1000 : Move?Move->ReachCm-10:130;
    if (Toward.Size() > Range)
    { bBlocking = Fighter == EAetherFighter::ShieldGuard && Equipment->GuardDefinition(); AddMovementInput(SafeMoveDirection(Target->GetActorLocation()), 1); }
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
        const float Regen = T > ActionUntil && !bBlocking && !Equipment->IsBusy() ? 20 : 4;
        SetVitals(Health(), Mana() + Dt * 5, Stamina() + Dt * Regen);
        if (T - LastDamageAt > 2) AbilitySystem->SetNumericAttributeBase(UAetherAttributes::GetPostureAttribute(), bUseBasicAssets ? FMath::Min(100.f, Attributes->Posture.GetCurrentValue() + Dt * 12) : FMath::Max(0.f, Attributes->Posture.GetCurrentValue() - Dt * 12));
        const double Temp = Reactive->State.TemperatureC;
        if (Temp > 55) { FDamageEvent E; TakeDamage(float(FMath::Min(25.0,(Temp - 55) * .12) * Dt), E, nullptr, Reactive->GetLastSourceActor()); }
        Think(Dt);
    }
    GetCharacterMovement()->MaxWalkSpeed = !Alive() || T < StunUntil ? 0.f : (bBlocking ? 220.f : Fighter == EAetherFighter::Player ? 450.f : Fighter==EAetherFighter::Wolf?380.f:230.f) * (Reactive->State.IceFraction > .5 ? .5f : 1.f);
    Tint(BodyVisual, !Alive() ? FLinearColor(.15f,.15f,.17f) : bWindingUp ? FLinearColor(1,.09f,.01f) : T < StunUntil ? FLinearColor(.1f,.8f,1) : Fighter == EAetherFighter::Player ? FLinearColor(.12f,.34f,.5f) : Fighter == EAetherFighter::FireCaster ? FLinearColor(.55f,.09f,.025f) : FLinearColor(.45f,.3f,.09f));
    UpdateAnimation();
    const TCHAR* N = Fighter == EAetherFighter::BellKnight ? TEXT("OLEN / BELL KNIGHT") : Fighter == EAetherFighter::ShieldGuard ? TEXT("SHIELD GUARD") : Fighter == EAetherFighter::FireCaster ? TEXT("EMBER CASTER") : TEXT("OATHFARER");
    Nameplate->SetText(FText::FromString(FString::Printf(TEXT("%s  %.0f\n%s"), N, Health(), bPacified ? TEXT("OATH RELEASED") : bWindingUp ? TEXT("ATTACK INCOMING") : TEXT(""))));
    Nameplate->SetVisibility(Fighter != EAetherFighter::Player);
    if (auto* PC = GetWorld()->GetFirstPlayerController(); PC && PC->PlayerCameraManager) Nameplate->SetWorldRotation((PC->PlayerCameraManager->GetCameraLocation() - Nameplate->GetComponentLocation()).Rotation());
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

AAetherProjectile::AAetherProjectile()
{
    PrimaryActorTick.bCanEverTick = true; bReplicates = true; SetReplicateMovement(true); SetNetUpdateFrequency(30);
    Visual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FireCore")); SetRootComponent(Visual);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    Visual->SetStaticMesh(Sphere.Object); Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision); Visual->SetRelativeScale3D(FVector(.28));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Surface(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    if (Surface.Succeeded()) Visual->SetMaterial(0,Surface.Object);
}
void AAetherProjectile::BeginPlay() { Super::BeginPlay(); Tint(Visual, FLinearColor(1,.12f,.005f)); }
void AAetherProjectile::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{ Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(AAetherProjectile, HeatJ); }
void AAetherProjectile::IntegrateWeather(double& Heat, FVector& V, const Reactive::FEnvironment& E, float Dt)
{ Heat = FMath::Max(0.0, Heat - (1500 + E.RainKgPerM2Sec * 220000) * Dt); V += E.WindMPerSec * 40 * Dt; }
void AAetherProjectile::Tick(float Dt)
{
    Super::Tick(Dt); Visual->SetRelativeScale3D(FVector(.08 + .2 * FMath::Clamp(HeatJ / 60000, 0.0, 1.0)));
    if (!HasAuthority()) return;
    Age += Dt;
    if (const auto* S = GetWorld()->GetSubsystem<UReactiveWorldSubsystem>()) IntegrateWeather(HeatJ, VelocityCm, S->GetSimulation()->GetEnvironment(), Dt);
    if (Age > 4 || HeatJ < 500) { Destroy(); return; }
    FHitResult Hit; const FVector End = GetActorLocation() + VelocityCm * Dt;
    FCollisionQueryParams P(SCENE_QUERY_STAT(AetherProjectile), false, this); P.AddIgnoredActor(GetOwner());
    if (GetWorld()->SweepSingleByChannel(Hit,GetActorLocation(),End,FQuat::Identity,ECC_Visibility,FCollisionShape::MakeSphere(12),P))
    {
        if(auto* Source=Cast<AAetherCharacter>(GetOwner());Source&&Source->Fighter==EAetherFighter::Player)if(auto* Other=Cast<AAetherCharacter>(Hit.GetActor());Other&&Other->Fighter==EAetherFighter::Player){Destroy();return;}
        if (AActor* A = Hit.GetActor()) if (auto* B = A->FindComponentByClass<UReactiveBodyComponent>())
        { FReactiveStimulus S; S.SourceActor = GetOwner(); S.HeatJ = HeatJ; S.ImpulseNs = VelocityCm.GetSafeNormal() * 2; B->Inject(S); }
        Destroy(); return;
    }
    SetActorLocation(End);
}

FVector AAetherCharacter::SafeMoveDirection(FVector Destination)
{
    if(CombatTime()<NextSteeringAt)return SteeringDirection;NextSteeringAt=CombatTime()+.2f;
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
    auto* FloorActor=GetCharacterMovement()->CurrentFloor.HitResult.GetActor();
    auto* EscapeIce=FloorActor?FloorActor->FindComponentByClass<UReactiveBodyComponent>():nullptr;
    if(EscapeIce&&EscapeIce->bIceControlsPawnCollision&&EscapeIce->IceSupport==EReactiveIceSupport::Thawing)
    {
        const FBox Box=EscapeIce->GetPrimitive()->Bounds.GetBox();const FVector Here=GetActorLocation();
        TArray<FVector> Exits={FVector(Box.Min.X-100,Here.Y,Here.Z),FVector(Box.Max.X+100,Here.Y,Here.Z),FVector(Here.X,Box.Min.Y-100,Here.Z),FVector(Here.X,Box.Max.Y+100,Here.Z)};
        Exits.Sort([&](const FVector& A,const FVector& B){return FVector::DistSquared2D(A,Here)<FVector::DistSquared2D(B,Here);});
        Waypoint=Exits[0];NextPathAt=0;NavigationPoints.Reset();
    }
    FVector Desired=(Waypoint-GetActorLocation()).GetSafeNormal2D();double Best=-1.e30;SteeringDirection=FVector::ZeroVector;
    FCollisionQueryParams Q(SCENE_QUERY_STAT(AetherSteering),false,this);
    auto* World=GetWorld()->GetSubsystem<UReactiveWorldSubsystem>();const auto* Sim=World?World->GetSimulation():nullptr;
    for(float Angle:{0.f,45.f,-45.f,90.f,-90.f,135.f,-135.f})
    {
        FVector Dir=Desired.RotateAngleAxis(Angle,FVector::UpVector);FVector End=GetActorLocation()+Dir*150;
        if(GetWorld()->SweepTestByChannel(GetActorLocation(),End,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(30,70),Q))continue;
        FHitResult Floor;if(!GetWorld()->LineTraceSingleByChannel(Floor,End,End-FVector(0,0,200),ECC_Visibility,Q)||Floor.ImpactNormal.Z<.5||!Floor.GetComponent()->IsCollisionEnabled()||Floor.GetComponent()->GetCollisionResponseToChannel(ECC_Pawn)!=ECR_Block)continue;
        if(auto* Bridge=Floor.GetActor()->FindComponentByClass<UAetherTraversalComponent>();Bridge&&Bridge->bAuthoredBridge&&!Bridge->bRouteOpen){NextPathAt=0;continue;}
        if(auto* Ice=Floor.GetActor()->FindComponentByClass<UReactiveBodyComponent>();Ice&&Ice->bIceControlsPawnCollision&&Ice->IceSupport!=EReactiveIceSupport::Bearing&&!(Ice==EscapeIce&&Ice->IceSupport==EReactiveIceSupport::Thawing)){NextPathAt=0;continue;}
        double Score=FVector::DotProduct(Dir,Desired);if(Sim)for(auto Id:Sim->Query(End,70))
        {const auto* State=Sim->Find(Id);if(State&&(State->bBurning||State->TemperatureC>100))Score-=5;}
        if(Score>Best){Best=Score;SteeringDirection=Dir;}
    }
    if(SteeringDirection.IsNearlyZero())NextPathAt=0;
    return SteeringDirection;
}
