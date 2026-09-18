#include "AetherFrontier.h"
#include "AetherContent.h"
#include "AetherRules.h"
#include "AetherInventoryRules.h"
#include "AetherActions.h"
#include "AetherInputProfile.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "PhysicsEngine/PhysicsHandleComponent.h"
#include "Components/InputComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Camera/CameraComponent.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/PlatformMisc.h"
#include "ReactiveWorldSubsystem.h"

AAetherFrontierCharacter::AAetherFrontierCharacter()
{ bUseBasicAssets=true; CarryHandle=CreateDefaultSubobject<UPhysicsHandleComponent>(TEXT("CarryHandle")); }
AAetherPlayerState* AAetherFrontierCharacter::ProfileState() const {return GetPlayerState<AAetherPlayerState>();}
void AAetherFrontierCharacter::BindPersistentAbilities()
{
    if (auto* PS=ProfileState())
    {
        if(HasAuthority())
        {
            if(AbilitySystem!=PS->AbilitySystem)
            {CancelActions();if(AbilitySystem&&AbilitySystem->GetAvatarActor()==this)AbilitySystem->ClearActorInfo();}
            if(auto* Old=Cast<AAetherCharacter>(PS->AbilitySystem->GetAvatarActor());Old&&Old!=this)Old->CancelActions();
        }
        AbilitySystem=PS->AbilitySystem; Attributes=PS->Attributes; AbilitySystem->InitAbilityActorInfo(PS,this);
    }
}
void AAetherFrontierCharacter::BeginPlay()
{
    BindPersistentAbilities(); Super::BeginPlay();
    Equipment->bProfileManaged=ProfileState()!=nullptr;
    if(HasAuthority())AbilitySystem->SetNumericAttributeBase(UAetherAttributes::GetPostureAttribute(),100);
    Equipment->OwnsItem.BindLambda([this](FName Id){auto* PS=ProfileState();return !PS || AetherInventory::OwnsEquipment(PS->Profile,Id,FAetherRules::Get());});
    Equipment->CanAct.BindLambda([this](){return Ready()&&!Carried&&!ReviveTarget&&!bPanel;});
    if (HasAuthority() && ProfileState()) ApplyProfileEquipment();
}
void AAetherFrontierCharacter::PossessedBy(AController* C)
{
    Super::PossessedBy(C); BindPersistentAbilities();
    if(HasAuthority() && ProfileState())
    { Equipment->bProfileManaged=true;GrantSpells();if(HasActorBegunPlay())ApplyProfileEquipment(); }
}
void AAetherFrontierCharacter::OnRep_PlayerState()
{ Super::OnRep_PlayerState(); BindPersistentAbilities(); }
bool AAetherFrontierCharacter::SpellUnlocked(int32 Spell) const
{ const auto* PS=ProfileState(); return Spell>=0 && Spell<4 && (!PS || (PS->Profile.LearnedSpells&(1<<Spell))!=0); }
void AAetherFrontierCharacter::ApplyProfileEquipment()
{
    auto* PS=ProfileState(); if (!HasAuthority()||!PS) return;
    TArray<FAetherEquippedSlot> Slots;
    if(!AetherInventory::BuildLoadout(PS->Profile,FAetherRules::Get(),Slots))return;
    Equipment->RestoreLoadout(Slots);
}
void AAetherFrontierCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(AAetherFrontierCharacter,bSprinting);
    DOREPLIFETIME(AAetherFrontierCharacter,Carried);DOREPLIFETIME(AAetherFrontierCharacter,CompanionOwner);
    DOREPLIFETIME(AAetherFrontierCharacter,CompanionId);DOREPLIFETIME(AAetherFrontierCharacter,bCompanionHold);DOREPLIFETIME(AAetherFrontierCharacter,EncounterId);DOREPLIFETIME(AAetherFrontierCharacter,BossPhase);DOREPLIFETIME(AAetherFrontierCharacter,BossVersion);DOREPLIFETIME(AAetherFrontierCharacter,BossPhaseStarted);DOREPLIFETIME(AAetherFrontierCharacter,BossPressure);DOREPLIFETIME(AAetherFrontierCharacter,bHealer);DOREPLIFETIME(AAetherFrontierCharacter,ReviveTarget);
}
void AAetherFrontierCharacter::Forward(float V){if(Alive()&&!bPanel)AddMovementInput(FRotationMatrix(FRotator(0,GetControlRotation().Yaw,0)).GetUnitAxis(EAxis::X),V);}
void AAetherFrontierCharacter::Right(float V){if(Alive()&&!bPanel)AddMovementInput(FRotationMatrix(FRotator(0,GetControlRotation().Yaw,0)).GetUnitAxis(EAxis::Y),V);}
void AAetherFrontierCharacter::Yaw(float V){if(!bPanel)AddControllerYawInput(V);}
void AAetherFrontierCharacter::Pitch(float V){if(!bPanel)AddControllerPitchInput(-V);}
void AAetherFrontierCharacter::PressAttack(){PressedAt=GetWorld()->GetTimeSeconds();}
void AAetherFrontierCharacter::ReleaseAttack(){if(!bPanel)ServerAttack(GetWorld()->GetTimeSeconds()-PressedAt>=.35f);}
void AAetherFrontierCharacter::SetupPlayerInputComponent(UInputComponent* I)
{
    auto* Enhanced=Cast<UEnhancedInputComponent>(I);auto* PC=Cast<APlayerController>(Controller);
    auto* Sub=PC&&PC->GetLocalPlayer()?ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()):nullptr;
    if(!Enhanced||!Sub)return;
    if(GameplayContext)Sub->RemoveMappingContext(GameplayContext);
    GameplayContext=NewObject<UInputMappingContext>(this);InputActions.Reset();
    auto Action=[&](FName Name,FKey Key,EInputActionValueType Type)
    {
        if(auto* Existing=InputActions.Find(Name))return Existing->Get();
        auto* A=NewObject<UInputAction>(this);A->ValueType=Type;A->bConsumeInput=false;InputActions.Add(Name,A);
        const auto* Override=GetDefault<UAetherInputProfile>()->Keys.Find(Name);GameplayContext->MapKey(A,Override?*Override:Key);return A;
    };
    auto Bind=[&](FName Name,FKey Key,ETriggerEvent Event,void(AAetherFrontierCharacter::*Fn)())
    {auto* A=Action(Name,Key,EInputActionValueType::Boolean);Enhanced->BindAction(A,Event,this,Fn);if(Event==ETriggerEvent::Completed)Enhanced->BindAction(A,ETriggerEvent::Canceled,this,Fn);};
    Bind("Tab",EKeys::Tab,ETriggerEvent::Started,&AAetherFrontierCharacter::CycleItem);
    Bind("Delete",EKeys::Delete,ETriggerEvent::Started,&AAetherFrontierCharacter::SellItem);
    Bind("Seven",EKeys::Seven,ETriggerEvent::Started,&AAetherFrontierCharacter::BuyMana);
    Bind("Eight",EKeys::Eight,ETriggerEvent::Started,&AAetherFrontierCharacter::BuyRation);
    Bind("Z",EKeys::Z,ETriggerEvent::Started,&AAetherFrontierCharacter::UseMana);
    Bind("F8",EKeys::F8,ETriggerEvent::Started,&AAetherFrontierCharacter::Recover);
    Bind("H",EKeys::H,ETriggerEvent::Started,&AAetherFrontierCharacter::PartyCommand);
    Bind("Delete",EKeys::Delete,ETriggerEvent::Started,&AAetherFrontierCharacter::Dismiss);
    Bind("Attack",EKeys::LeftMouseButton,ETriggerEvent::Started,&AAetherFrontierCharacter::PressAttack);
    Bind("Attack",EKeys::LeftMouseButton,ETriggerEvent::Completed,&AAetherFrontierCharacter::ReleaseAttack);
    Bind("Guard",EKeys::RightMouseButton,ETriggerEvent::Started,&AAetherFrontierCharacter::GuardOn);
    Bind("Guard",EKeys::RightMouseButton,ETriggerEvent::Completed,&AAetherFrontierCharacter::GuardOff);
    Bind("Sprint",EKeys::LeftShift,ETriggerEvent::Started,&AAetherFrontierCharacter::SprintOn);
    Bind("Sprint",EKeys::LeftShift,ETriggerEvent::Completed,&AAetherFrontierCharacter::SprintOff);
    Bind("Dodge",EKeys::SpaceBar,ETriggerEvent::Started,&AAetherFrontierCharacter::Dodge);
    Bind("Jump",EKeys::LeftControl,ETriggerEvent::Started,&AAetherFrontierCharacter::JumpV4);
    Bind("Jump",EKeys::LeftControl,ETriggerEvent::Completed,&AAetherFrontierCharacter::StopJumping);
    Bind("One",EKeys::One,ETriggerEvent::Started,&AAetherFrontierCharacter::Spell0);
    Bind("Two",EKeys::Two,ETriggerEvent::Started,&AAetherFrontierCharacter::Spell1);
    Bind("Three",EKeys::Three,ETriggerEvent::Started,&AAetherFrontierCharacter::Spell2);
    Bind("Four",EKeys::Four,ETriggerEvent::Started,&AAetherFrontierCharacter::Spell3);
    Bind("Cast",EKeys::MiddleMouseButton,ETriggerEvent::Started,&AAetherFrontierCharacter::CastSelectedV4);
    Bind("Interact",EKeys::E,ETriggerEvent::Started,&AAetherFrontierCharacter::InteractV4);
    Bind("Potion",EKeys::Q,ETriggerEvent::Started,&AAetherFrontierCharacter::UsePotion);
    Bind("Carry",EKeys::G,ETriggerEvent::Started,&AAetherFrontierCharacter::Carry);
    Bind("Push",EKeys::V,ETriggerEvent::Started,&AAetherFrontierCharacter::Push);
    Bind("R",EKeys::R,ETriggerEvent::Started,&AAetherFrontierCharacter::EquipNext);
    Bind("T",EKeys::T,ETriggerEvent::Started,&AAetherFrontierCharacter::Shield);
    Bind("F5",EKeys::F5,ETriggerEvent::Started,&AAetherFrontierCharacter::SaveV4);
    Bind("B",EKeys::B,ETriggerEvent::Started,&AAetherFrontierCharacter::SplitStack);
    Bind("N",EKeys::N,ETriggerEvent::Started,&AAetherFrontierCharacter::MergeStacks);
    Bind("I",EKeys::I,ETriggerEvent::Started,&AAetherFrontierCharacter::ToggleInventory);
    Bind("J",EKeys::J,ETriggerEvent::Started,&AAetherFrontierCharacter::ToggleQuests);
    Bind("K",EKeys::K,ETriggerEvent::Started,&AAetherFrontierCharacter::ToggleSkills);
    Bind("M",EKeys::M,ETriggerEvent::Started,&AAetherFrontierCharacter::ToggleMap);
    Bind("P",EKeys::P,ETriggerEvent::Started,&AAetherFrontierCharacter::ToggleParty);
    Bind("Escape",EKeys::Escape,ETriggerEvent::Started,&AAetherFrontierCharacter::ToggleMenu);
    Bind("Debug",EKeys::F10,ETriggerEvent::Started,&AAetherFrontierCharacter::ToggleDebug);
    Bind("Weather",EKeys::F11,ETriggerEvent::Started,&AAetherFrontierCharacter::ToggleWeather);
    Bind("Throw",EKeys::C,ETriggerEvent::Started,&AAetherFrontierCharacter::Throw);
    Bind("Claim",EKeys::Enter,ETriggerEvent::Started,&AAetherFrontierCharacter::ClaimRewards);
    Bind("Invite",EKeys::Y,ETriggerEvent::Started,&AAetherFrontierCharacter::Invite);
    Bind("AcceptInvite",EKeys::U,ETriggerEvent::Started,&AAetherFrontierCharacter::AcceptInvite);
    Bind("LeaveParty",EKeys::O,ETriggerEvent::Started,&AAetherFrontierCharacter::LeaveParty);
    Bind("Lock",EKeys::F,ETriggerEvent::Started,&AAetherFrontierCharacter::ToggleLock);
    for(auto Pair:{TPair<FName,FKey>("Forward",EKeys::W),TPair<FName,FKey>("Backward",EKeys::S),TPair<FName,FKey>("Left",EKeys::A),TPair<FName,FKey>("Right",EKeys::D)})
    {
        auto* A=Action(Pair.Key,Pair.Value,EInputActionValueType::Boolean);const FName Name=Pair.Key;
        Enhanced->BindActionValueLambda(A,ETriggerEvent::Triggered,[this,Name](const FInputActionValue&){if(Name=="Forward")Forward(1);else if(Name=="Backward")Forward(-1);else Right(Name=="Left"?-1:1);});
    }
    Enhanced->BindActionValueLambda(Action("LookX",EKeys::MouseX,EInputActionValueType::Axis1D),ETriggerEvent::Triggered,[this](const FInputActionValue& V){Yaw(V.Get<float>());});
    Enhanced->BindActionValueLambda(Action("LookY",EKeys::MouseY,EInputActionValueType::Axis1D),ETriggerEvent::Triggered,[this](const FInputActionValue& V){Pitch(V.Get<float>());});
    Sub->AddMappingContext(GameplayContext,0);
}
FKey AAetherFrontierCharacter::BindingFor(FName Name) const
{
 const auto* A=InputActions.Find(Name);if(A&&GameplayContext)for(const auto& M:GameplayContext->GetMappings())if(M.Action==A->Get())return M.Key;return FKey();
}
void AAetherFrontierCharacter::AetherBind(FName Name,FKey Key)
{
 if(!IsLocallyControlled()||!GameplayContext||!Key.IsValid()||Key.IsAxis1D()||Key.IsAxis2D()||Name=="LookX"||Name=="LookY"||Name=="Escape"||Key==EKeys::Escape)return;
 auto* A=InputActions.Find(Name);if(!A)return;const FKey Old=BindingFor(Name);if(Old==Key)return;
 auto* Profile=GetMutableDefault<UAetherInputProfile>();
 // A single mapping per action: swap conflicts so no command is silently orphaned.
 TArray<FName> Conflicts;for(const auto& Pair:InputActions)if(Pair.Key!=Name&&BindingFor(Pair.Key)==Key)Conflicts.Add(Pair.Key);
 for(auto Other:Conflicts){auto* OtherAction=InputActions[Other].Get();GameplayContext->UnmapAllKeysFromAction(OtherAction);GameplayContext->MapKey(OtherAction,Old);Profile->Keys.Add(Other,Old);}
 GameplayContext->UnmapAllKeysFromAction(A->Get());GameplayContext->MapKey(A->Get(),Key);Profile->Keys.Add(Name,Key);Profile->SaveConfig();
 Feedback=FString::Printf(TEXT("Bound %s: %s. Conflicting binding swapped."),*Name.ToString(),*Key.GetDisplayName().ToString());
 if(auto* PC=Cast<APlayerController>(Controller))if(auto* Sub=ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))Sub->RequestRebuildControlMappings();
}
void AAetherFrontierCharacter::ToggleLock()
{
    if(bPanel)return;if(LockedTarget){LockedTarget=nullptr;return;}
    double Best=FMath::Square(1500.);
    for(TActorIterator<AAetherCharacter> It(GetWorld());It;++It)if(It->Fighter!=EAetherFighter::Player&&It->Alive())
    {double D=FVector::DistSquared(It->GetActorLocation(),GetActorLocation());if(D<Best){FCollisionQueryParams Q(SCENE_QUERY_STAT(Lock),false,this);Q.AddIgnoredActor(*It);if(!GetWorld()->LineTraceTestByChannel(GetActorLocation(),It->GetActorLocation(),ECC_Visibility,Q)){Best=D;LockedTarget=*It;}}}
}
void AAetherFrontierCharacter::ServerSprint_Implementation(bool Enabled){bSprinting=Enabled&&Alive()&&Stamina()>5;}
void AAetherFrontierCharacter::Notify_Implementation(const FString& Message){Feedback=Message;}
void AAetherFrontierCharacter::ReleaseCarry()
{
    if(!HasAuthority())return; CarryHandle->ReleaseComponent();
    if(Carried){Carried->Mechanism->RecordImpactSource(this);Carried->Carrier=nullptr; Carried->Mesh->IgnoreActorWhenMoving(this,false);GetCapsuleComponent()->IgnoreActorWhenMoving(Carried,false); Carried->ForceNetUpdate();}
    Carried=nullptr;
}
void AAetherFrontierCharacter::EndPlay(const EEndPlayReason::Type Reason){ReleaseCarry();Super::EndPlay(Reason);}
void AAetherFrontierCharacter::ServerAction_Implementation(FName Action,int32 Index)
{
    auto* Mode=GetWorld()->GetAuthGameMode<AAetherFrontierMode>(); auto* PS=ProfileState(); if(!Mode||!PS)return;
    if(CombatTime()<NextServerAction)return;NextServerAction=CombatTime()+.12f;
    if(Action=="Weather")
    {
#if !UE_BUILD_SHIPPING
        if(GetNetMode()==NM_Standalone||(Controller&&Controller->IsLocalController()))
        {auto* S=GetWorld()->GetGameState<AAetherFrontierState>();S->bRain=!S->bRain;Notify(S->bRain?TEXT("Rain: thermal and visual channels only."):TEXT("Clear weather."));}
#endif
        return;
    }
    if(Action=="Recover")
    {
        if(Alive()||TimeSinceDamage()<3)return;
        ReleaseCarry();SetVitals(MaxHealth,100,100);ResetCombat();
        SetActorLocation(PS->Profile.bRegistered?FVector(-500,-500,120):FVector(-6500,-29000,120));return;
    }
    if(Action=="Save"){Notify(Mode->SaveWorld()?TEXT("World and profiles saved."):TEXT("Save deferred: world has pending reactions."));return;}
    if(!Alive())return;
    if(Action=="Interact"){Notify(Mode->Interact(this));return;}
    if((Action=="Invite"||Action=="AcceptInvite"||Action=="LeaveParty"||Action=="Dismiss")&&!Mode->CanChangeParty(this))
    {Notify(TEXT("Change party at town, out of combat and outside active encounters."));return;}
    if(Action=="Invite")
    {
        if(PS->PartyLeader!=PS->Profile.CharacterId)return;AAetherPlayerState* Target=nullptr;double Best=FMath::Square(500.);
        for(TActorIterator<AAetherFrontierCharacter> It(GetWorld());It;++It)if(*It!=this&&It->ProfileState()&&It->ProfileState()->PartyLeader!=PS->PartyLeader&&Mode->CanChangeParty(*It))
        {double D=FVector::DistSquared(GetActorLocation(),It->GetActorLocation());if(D<Best){Best=D;Target=It->ProfileState();}}
        if(Target){Target->InvitedBy=PS->Profile.CharacterId;Target->InvitationExpires=CombatTime()+30;Notify(TEXT("Party invitation sent; recipient P + U accepts within 30 seconds."));}return;
    }
    if(Action=="AcceptInvite")
    {
        if(PS->InvitedBy.IsEmpty()||CombatTime()>PS->InvitationExpires)return;
        for(TActorIterator<AAetherPlayerState> It(GetWorld());It;++It)if(It->Profile.CharacterId==PS->InvitedBy&&It->PartyLeader==It->Profile.CharacterId&&Mode->CanChangeParty(Cast<AAetherFrontierCharacter>(It->GetPawn())))
        {const FString Leader=It->PartyLeader;Mode->LeaveParty(PS);PS->PartyLeader=Leader;break;}
        PS->InvitedBy.Empty();return;
    }
    if(Action=="LeaveParty"){Mode->LeaveParty(PS);return;}
    if(Action=="PartyCommand"||Action=="Dismiss")
    {if(Action=="PartyCommand"&&Mode->Encounters&&Mode->Encounters->Abbey.Phase==EAetherEncounterPhase::Channel&&FVector::DistSquared(GetActorLocation(),Mode->Prop("AbbeyValve")->GetActorLocation())<FMath::Square(300.)){Notify(Mode->Encounters->Channel(this,true));return;}for(const auto& B:Mode->Companions)if(IsValid(B)&&B->CompanionOwner==this){if(Action=="Dismiss")B->Destroy();else B->bCompanionHold=!B->bCompanionHold;}return;}
    if(Action=="Recruit"){Notify(Mode->RecruitCompanion(this));return;}
    if(Action=="Throw")
    {if(Carried){auto* P=Carried.Get();ReleaseCarry();P->Mesh->AddImpulse(GetControlRotation().Vector()*P->Mesh->GetMass()*500);P->Mechanism->RecordImpactSource(this);}return;}
    if(Action=="Claim")
    {auto Next=PS->Profile;bool Changed=Next.CollectPending();Changed|=AetherQuests::Settle(Next,Mode->Database->WorldFacts,true);if(Changed)Notify(Mode->Commit(PS,Next)?TEXT("Pending rewards received."):TEXT("Reward save failed; retry."));return;}
    if(Action=="Carry"||Action=="Push")
    {
        if(Carried){ReleaseCarry();return;}
        if(!Ready())return;
        FHitResult H;FCollisionQueryParams Q(SCENE_QUERY_STAT(Carry),false,this);
        GetWorld()->LineTraceSingleByChannel(H,GetActorLocation()+FVector(0,0,25),GetActorLocation()+FVector(0,0,25)+GetControlRotation().Vector()*220,ECC_Visibility,Q);
        auto* P=Cast<AAetherFrontierProp>(H.GetActor());
        if(!P||!P->bCarryable||P->Carrier||P->Reactive->State.bBroken||!P->Mesh->IsSimulatingPhysics()||P->Mesh->GetMass()>80)return;
        P->Mechanism->RecordImpactSource(this);if(Action=="Push"){P->Mesh->AddImpulse(GetActorForwardVector()*15000);return;}
        P->Carrier=this;Carried=P;P->Mesh->IgnoreActorWhenMoving(this,true);GetCapsuleComponent()->IgnoreActorWhenMoving(P,true);
        CarryHandle->GrabComponentAtLocationWithRotation(P->Mesh,NAME_None,P->GetActorLocation(),P->GetActorRotation());return;
    }
    auto Next=PS->Profile;
    if(Action=="EquipInstance")
    {if(!Ready()||Carried||!Next.Inventory.IsValidIndex(Index)||!Next.Equip(Next.Inventory[Index].InstanceId))return;if(Mode->Commit(PS,Next))ApplyProfileEquipment();return;}
    if(Action=="UseSelected")
    {
        if(!Next.Inventory.IsValidIndex(Index)||CombatTime()<NextPotion)return;const auto Id=Next.Inventory[Index].DefinitionId;
        if(Id!="Potion"&&Id!="ManaPotion"&&Id!="Ration")return;if(Id=="Ration"&&TimeSinceDamage()<8)return;
        if(!Next.Remove(Id,1))return;if(Mode->Commit(PS,Next)){SetVitals(Health()+(Id=="Potion"?40:Id=="Ration"?15:0),Mana()+(Id=="ManaPotion"?40:0),Stamina()+(Id=="Ration"?25:0));NextPotion=CombatTime()+3;}return;
    }
    if(Action=="Potion"||Action=="ManaPotion")
    {
        if(CombatTime()<NextPotion)return;bool ManaItem=Action=="ManaPotion";
        if((ManaItem?Mana()>=100:Health()>=MaxHealth)||!Next.Remove(ManaItem?"ManaPotion":"Potion",1))return;
        if(Mode->Commit(PS,Next)){SetVitals(Health()+(ManaItem?0:40),Mana()+(ManaItem?40:0),Stamina());NextPotion=CombatTime()+3;}return;
    }
    if(Action=="Buy"||Action=="Sell")
    {
        auto* Shop=Mode->Prop("Shop");if(!Shop||FVector::DistSquared(GetActorLocation(),Shop->GetActorLocation())>FMath::Square(260.))return;
        if(Action=="Buy")
        {
            const FName Items[]={"Potion","ManaPotion","Ration"};if(Index<0||Index>2)return;const auto* Rule=FAetherRules::Get().Items.Find(Items[Index]);
            if(!Rule||Rule->Buy<=0||Next.Gold<Rule->Buy||!Next.Add(Items[Index],1))return;Next.Gold-=Rule->Buy;
        }
        else
        {
            if(!Next.Inventory.IsValidIndex(Index))return;const auto Item=Next.Inventory[Index];const auto* Rule=FAetherRules::Get().Items.Find(Item.DefinitionId);
            if(!Rule||!Rule->bSellable||Rule->Sell<=0||Next.Equipped.FindKey(Item.InstanceId)||Next.Gold>10000000-Rule->Sell||!Next.Remove(Item.DefinitionId,1))return;Next.Gold+=Rule->Sell;
        }
        Notify(Mode->Commit(PS,Next)?TEXT("Trade saved."):TEXT("Trade failed; no items or gold changed."));return;
    }
    if(Action=="Equip"||Action=="Shield")
    {
        if(!Ready()||Carried)return;
        TArray<FGuid> Choices;for(const auto& S:Next.Inventory)
            if(const auto* R=FAetherRules::Get().Items.Find(S.DefinitionId);R&&R->bPlayerEquippable&&R->Slot==(Action=="Shield"?FName("OffHand"):FName("MainHand")))Choices.Add(S.InstanceId);
        if(Choices.IsEmpty())return;
        const FName Slot=Action=="Shield"?FName("OffHand"):FName("MainHand");
        if(Action=="Shield"&&Next.Equipped.Contains(Slot))Next.Equipped.Remove(Slot);
        else {int32 Current=Choices.Find(Next.Equipped.FindRef(Slot));if(!Next.Equip(Choices[(Current+1)%Choices.Num()]))return;}
    }
    else if(Action=="Split")
    {const auto* Stack=Next.Inventory.FindByPredicate([](const auto& S){return S.Count>1;});if(!Stack||!Next.Split(Stack->InstanceId,1))return;}
    else if(Action=="Merge")
    {bool Merged=false;for(int32 A=0;A<Next.Inventory.Num()&&!Merged;++A)for(int32 B=A+1;B<Next.Inventory.Num()&&!Merged;++B)if(Next.Inventory[A].DefinitionId==Next.Inventory[B].DefinitionId)Merged=Next.Merge(Next.Inventory[B].InstanceId,Next.Inventory[A].InstanceId);if(!Merged)return;}
    else return;
    if(Mode->Commit(PS,Next))ApplyProfileEquipment();
}
void AAetherFrontierCharacter::ReceiveEquipmentHit_Implementation(const FAetherEquipmentHit& Hit)
{
    if(Reactive->bOwnerOnlyStimuli&&Hit.Source!=GetOwner())return;
    if(auto* Other=Cast<AAetherCharacter>(Hit.Source);Other&&Other->Reactive->bOwnerOnlyStimuli&&Other->GetOwner()!=this)return;
    const bool Blocked=bBlocking&&Equipment->GuardDefinition()&&Hit.Source&&FVector::DotProduct(GetActorForwardVector(),(Hit.Source->GetActorLocation()-GetActorLocation()).GetSafeNormal())>.25;
    if(const auto* Source=::Cast<AAetherCharacter>(Hit.Source); Source && Source->Fighter==EAetherFighter::Player && Fighter==EAetherFighter::Player)return;
    Super::ReceiveEquipmentHit_Implementation(Hit);
    if(auto* M=GetWorld()->GetAuthGameMode<AAetherFrontierMode>())
    { M->CreditHit(this,Cast<AAetherCharacter>(Hit.Source)); if(Blocked)M->Observe(this,"Block"); }
}
void AAetherFrontierCharacter::Tick(float Dt)
{
    Super::Tick(Dt);
    if(IsLocallyControlled()&&LockedTarget)
    {
        if(!IsValid(LockedTarget)||!LockedTarget->Alive()||FVector::DistSquared(GetActorLocation(),LockedTarget->GetActorLocation())>FMath::Square(1800.))LockedTarget=nullptr;
        else if(!bPanel&&Controller)Controller->SetControlRotation(FMath::RInterpTo(GetControlRotation(),(LockedTarget->GetActorLocation()-GetActorLocation()).Rotation(),Dt,7));
    }
    if(bSprinting && Alive()&&!bBlocking&&CombatTime()>=StunUntil&&Stamina()>0)
    {
        GetCharacterMovement()->MaxWalkSpeed=625;
        if(HasAuthority()&&!GetVelocity().IsNearlyZero()){AbilitySystem->ApplyModToAttribute(UAetherAttributes::GetStaminaAttribute(),EGameplayModOp::Additive,-32*Dt);if(Stamina()<1)bSprinting=false;}
    }
    if(!HasAuthority() && IsLocallyControlled() && FParse::Param(FCommandLine::Get(),TEXT("AetherV4NetClient")))
    {
        static float TestTime=0;static float RequestTime=0;TestTime+=Dt;
        auto* PS=ProfileState();auto* S=GetWorld()->GetGameState<AAetherFrontierState>();
        if(PS && TestTime-RequestTime>.5f){ServerAction("Interact");RequestTime=TestTime;}
        if(PS&&S&&TestTime>4&&PS->Profile.Evidence.Contains("SupplyA")&&PS->Profile.Count("Supply")==1&&S->bSupplyRestored)
        {
            const bool DataFixture=FParse::Param(FCommandLine::Get(),TEXT("AetherV802Net"));
            const auto* Weapon=Equipment->InSlot("MainHand");
            const bool EquipmentValid=DataFixture?PS->Profile.Count("SurveySword")==1&&PS->Profile.Equipped.Contains("MainHand")&&Weapon&&Weapon->ItemId=="TrainingSword"&&Equipment->VisualForSlot("MainHand"):Equipment->Slots.IsEmpty();
            bool Pass=PS->Profile.Claims.IsEmpty()&&!PS->Profile.Evidence.Contains("SupplyRestored")&&AbilitySystem==PS->AbilitySystem&&AbilitySystem->GetOwnerActor()==PS
                &&EquipmentValid&&!SpellUnlocked(0)&&GetWorld()->GetSubsystem<UReactiveWorldSubsystem>()->GetSimulation()->GetStats().Registered==0;
            UE_LOG(LogTemp,Display,TEXT("AETHER_V4_NET_%s profile=%s revision=%d isolated=%d claims=%d evidence=%d asc=%d owner=%d slots=%d locked=%d bodies=%d"),Pass?TEXT("PASS"):TEXT("FAIL"),*PS->Profile.CharacterId,PS->Profile.Revision,Pass,PS->Profile.Claims.Num(),PS->Profile.Evidence.Contains("SupplyRestored"),AbilitySystem==PS->AbilitySystem,AbilitySystem->GetOwnerActor()==PS,Equipment->Slots.Num(),!SpellUnlocked(0),GetWorld()->GetSubsystem<UReactiveWorldSubsystem>()->GetSimulation()->GetStats().Registered);
            if(DataFixture)UE_LOG(LogTemp,Display,TEXT("AETHER_V802_EQUIPMENT_%s profile=%s"),Pass?TEXT("PASS"):TEXT("FAIL"),*PS->Profile.CharacterId);
            FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
        }
        if(TestTime>30){UE_LOG(LogTemp,Display,TEXT("AETHER_V4_NET_FAIL timeout"));FPlatformMisc::RequestExitWithStatus(false,1);}
    }
    if(!HasAuthority())return;
    if(auto* PS=ProfileState())MaxHealth=100+5*FMath::Clamp(PS->Profile.Experience/200,0,4);
    if(Carried)
    {
        if(!Ready()||FVector::DistSquared(GetActorLocation(),Carried->GetActorLocation())>FMath::Square(350.0))ReleaseCarry();
        else
        {
            const FVector From=GetActorLocation()+FVector(0,0,25);FVector Target=From+GetControlRotation().Vector()*160;
            FHitResult Block;FCollisionQueryParams Q(SCENE_QUERY_STAT(CarrySweep),false,this);Q.AddIgnoredActor(Carried);
            if(GetWorld()->SweepSingleByChannel(Block,From,Target,FQuat::Identity,ECC_WorldStatic,FCollisionShape::MakeSphere(35),Q))Target=Block.Location;
            CarryHandle->SetTargetLocationAndRotation(Target,GetActorRotation());
        }
    }
    if(ReviveTarget)
    {
        if(!IsValid(ReviveTarget)||!Alive()||ReviveTarget->Alive()||DamageReceivedCount!=ReviveDamageSerial||CombatTime()<StunUntil||FVector::DistSquared(GetActorLocation(),ReviveTarget->GetActorLocation())>FMath::Square(220.0))ReviveTarget=nullptr;
        if(ReviveTarget){FCollisionQueryParams Q(SCENE_QUERY_STAT(RescueLOS),false,this);Q.AddIgnoredActor(ReviveTarget);if(GetWorld()->LineTraceTestByChannel(GetActorLocation(),ReviveTarget->GetActorLocation(),ECC_Visibility,Q))ReviveTarget=nullptr;}
        if(!ReviveTarget)if(auto* Spec=AbilitySystem->FindAbilitySpecFromClass(UAetherReviveAbility::StaticClass()))AbilitySystem->CancelAbilityHandle(Spec->Handle);
    }
    if(!IsValid(CompanionOwner)||!Alive()||CombatTime()<StunUntil||bCompanionHold)return;
    if(auto* M=GetWorld()->GetAuthGameMode<AAetherFrontierMode>();M&&M->Encounters&&M->Encounters->IsChanneling(this))return;
    const float T=CombatTime();
    if(!CompanionOwner->Alive())
    {
        const FVector D=CompanionOwner->GetActorLocation()-GetActorLocation();
        if(D.Size2D()>170)AddMovementInput(SafeMoveDirection(CompanionOwner->GetActorLocation()));
        else if(!ReviveTarget){ReviveTarget=CompanionOwner;ReviveStarted=T;ReviveDamageSerial=DamageReceivedCount;if(!AbilitySystem->TryActivateAbilityByClass(UAetherReviveAbility::StaticClass()))ReviveTarget=nullptr;}
        return;
    }
    if(bHealer&&T>NextCompanionAction&&CompanionOwner->Health()<65&&Mana()>=15&&FVector::DistSquared(GetActorLocation(),CompanionOwner->GetActorLocation())<FMath::Square(600.0))
    {CompanionOwner->SetVitals(CompanionOwner->Health()+20,CompanionOwner->Mana(),CompanionOwner->Stamina());AbilitySystem->ApplyModToAttribute(UAetherAttributes::GetManaAttribute(),EGameplayModOp::Additive,-15);NextCompanionAction=T+5;}
    AAetherCharacter* Target=nullptr;double Best=FMath::Square(900.0);
    for(TActorIterator<AAetherCharacter> It(GetWorld());It;++It)if(It->Fighter!=EAetherFighter::Player&&It->Alive())
    {double D=FVector::DistSquared(GetActorLocation(),It->GetActorLocation());if(D<Best)
     {auto* FC=Cast<AAetherFrontierCharacter>(*It);auto* M=GetWorld()->GetAuthGameMode<AAetherFrontierMode>();if(FC&&!FC->EncounterId.IsNone()&&M&&M->Encounters&&!M->Encounters->Participates(this,FC->EncounterId))continue;
      FCollisionQueryParams Q(SCENE_QUERY_STAT(CompanionSight),false,this);Q.AddIgnoredActor(*It);if(GetWorld()->LineTraceTestByChannel(GetActorLocation(),It->GetActorLocation(),ECC_Visibility,Q))continue;Best=D;Target=*It;}}
    const FVector D=(Target?Target->GetActorLocation():CompanionOwner->GetActorLocation())-GetActorLocation();
    if(!Target){if(D.Size2D()>400)bFollowing=true;else if(D.Size2D()<250)bFollowing=false;}
    if(Target?D.Size2D()>145:bFollowing)
    {
        FHitResult Wall;FCollisionQueryParams Q(SCENE_QUERY_STAT(CompanionMove),false,this);Q.AddIgnoredActor(CompanionOwner);if(Target)Q.AddIgnoredActor(Target);
        FVector Dir=D.GetSafeNormal2D();
        if(GetWorld()->LineTraceSingleByChannel(Wall,GetActorLocation(),GetActorLocation()+Dir*160,ECC_Visibility,Q))Dir=FVector::CrossProduct(Wall.ImpactNormal,FVector::UpVector).GetSafeNormal();
        AddMovementInput(SafeMoveDirection(Target?Target->GetActorLocation():CompanionOwner->GetActorLocation()));
    }
    if(Target){SetActorRotation(D.Rotation());if(Controller)Controller->SetControlRotation(D.Rotation());if(T>NextCompanionAction&&Ready()){PerformMelee(false);NextCompanionAction=T+1;}}
}

float AAetherFrontierCharacter::TakeDamage(float Amount,const FDamageEvent& Event,AController* EventInstigator,AActor* Causer)
{
    auto* SourceCharacter=Cast<AAetherFrontierCharacter>(Causer);
    if(Reactive->bOwnerOnlyStimuli&&Causer!=GetOwner()&&(!EventInstigator||EventInstigator->GetPawn()!=GetOwner()))return 0;
    if(SourceCharacter&&SourceCharacter->Reactive->bOwnerOnlyStimuli&&SourceCharacter->GetOwner()!=this)return 0;
    if(!SourceCharacter&&EventInstigator)SourceCharacter=Cast<AAetherFrontierCharacter>(EventInstigator->GetPawn());
    if(!EncounterId.IsNone()&&Fighter!=EAetherFighter::Player&&SourceCharacter&&SourceCharacter->Fighter==EAetherFighter::Player)
        if(auto* M=GetWorld()->GetAuthGameMode<AAetherFrontierMode>();M&&M->Encounters&&!M->Encounters->Participates(SourceCharacter,EncounterId))return 0;
    if(Fighter==EAetherFighter::BellKnight&&BossPhase!=2)Amount*=.65f;
    const float Applied=Super::TakeDamage(Amount,Event,EventInstigator,Causer);
    if(Applied>0)
    {
        auto* Source=Cast<AAetherCharacter>(Causer);if(!Source&&EventInstigator)Source=Cast<AAetherCharacter>(EventInstigator->GetPawn());
        if(auto* Mode=GetWorld()->GetAuthGameMode<AAetherFrontierMode>())Mode->CreditHit(this,Source);
        if(Fighter==EAetherFighter::BellKnight)BossPressure+=Applied*.25f;
    }
    return Applied;
}

void AAetherFrontierCharacter::CycleItem()
{
 if(!bPanel||!ProfileState())return;
 if(Panel==1&&!ProfileState()->Profile.Inventory.IsEmpty())SelectedItem=(SelectedItem+1)%ProfileState()->Profile.Inventory.Num();
 if(Panel==2)TrackedQuest=AetherGuide::SelectQuest(ProfileState()->Profile,TrackedQuest,true);
}
