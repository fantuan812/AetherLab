#include "AetherFrontier.h"
#include "AetherPhysicsDamage.h"
#include "AetherTraversal.h"
#include "AetherContent.h"
#include "AetherRules.h"
#include "AetherInventoryRules.h"
#include "AetherWorldAuthoring.h"
#include "NavigationSystem.h"
#include "GameFramework/WorldSettings.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"
#include "ReactiveWorldSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/PlatformMisc.h"
#include "UnrealClient.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/Crc.h"
#include "HAL/FileManager.h"

AAetherFrontierProp::AAetherFrontierProp()
{
    Mechanism=CreateDefaultSubobject<UReactiveMechanismComponent>(TEXT("Mechanism"));
    Traversal=CreateDefaultSubobject<UAetherTraversalComponent>(TEXT("Traversal"));
    PhysicsDamage=CreateDefaultSubobject<UAetherPhysicsDamageComponent>(TEXT("PhysicsDamage"));
    Person=CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("UEMannequin"));Person->SetupAttachment(Mesh);
    Person->SetAbsolute(false,true,true);Person->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}
void AAetherFrontierProp::BeginPlay()
{
    Super::BeginPlay();Reactive->OnReaction.AddDynamic(this,&AAetherFrontierProp::OnMaterialReaction);
    Reactive->OnElectricalWindow.AddDynamic(this,&AAetherFrontierProp::OnElectricalWindow);
    if(Service=="Register"||Service=="Inn"||Service=="Teacher"||Service=="Shop"||Service=="Recruit"||Service=="Rescue"||Service=="SealDelivered")
    {
        Person->SetSkeletalMesh(LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple")));
        Person->SetWorldLocation(GetActorLocation()-FVector(0,0,90));Person->SetWorldRotation(FRotator(0,-90,0));
        if(auto* Idle=LoadObject<UAnimSequence>(nullptr,TEXT("/Game/Characters/Mannequins/Anims/Unarmed/MM_Idle.MM_Idle"),nullptr,LOAD_NoWarn))Person->PlayAnimation(Idle,true);
    }
    if(bCarryable&&HasAuthority()){Mesh->SetSimulatePhysics(true);Mesh->SetMassOverrideInKg(NAME_None,20,true);}
}
void AAetherFrontierProp::Tick(float Dt)
{
    Super::Tick(Dt);
    if(Person->GetSkeletalMeshAsset())Mesh->SetVisibility(false,false);
    UpdateReactionFeedback();
    if(HasAuthority()&&GetWorld()->GetTimeSeconds()-LastPowerTime>.3)ReceivedPower=0;
}
void AAetherFrontierProp::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);DOREPLIFETIME(AAetherFrontierProp,bGlobalPowerService);DOREPLIFETIME(AAetherFrontierProp,bWorkshopService);DOREPLIFETIME(AAetherFrontierProp,bExtinguished);DOREPLIFETIME(AAetherFrontierProp,Service);DOREPLIFETIME(AAetherFrontierProp,bCarryable);
    DOREPLIFETIME(AAetherFrontierProp,Carrier);DOREPLIFETIME(AAetherFrontierProp,ReceivedPower);DOREPLIFETIME(AAetherFrontierProp,bAcceptsWater);DOREPLIFETIME(AAetherFrontierProp,bInspectableFire);
}
void AAetherFrontierProp::ReceiveEquipmentHit_Implementation(const FAetherEquipmentHit& Hit)
{
    if(!HasAuthority())return;
    if(Service=="Dummy")
    {
        if(Hit.AttackId!="Light")return;
        if(auto* M=GetWorld()->GetAuthGameMode<AAetherFrontierMode>())if(auto* C=Cast<AAetherFrontierCharacter>(Hit.Source))if(auto* PS=C->ProfileState())
            for(FName F:{FName("Melee1"),FName("Melee2"),FName("Melee3")})if(!PS->Profile.Evidence.Contains(F)){M->Observe(C,F);break;}
        return;
    }
    if(Spec.bInteractiveMaterial)Super::ReceiveEquipmentHit_Implementation(Hit);
}
void AAetherFrontierProp::OnElectricalWindow(const FReactiveElectricalWindow& Window)
{
    if(!HasAuthority())return;
    ReceivedPower=float(Window.UsefulPowerW());LastPowerTime=GetWorld()->GetTimeSeconds();
}
void AAetherFrontierProp::OnMaterialReaction(EReactiveReaction K,double Magnitude,FVector Vector)
{
    if(!HasAuthority())return;
    if(K==EReactiveReaction::Ignited){bWasBurning=true;bExtinguished=false;}
    if(K==EReactiveReaction::Extinguished && bWasBurning)
    {
        if(auto* M=GetWorld()->GetAuthGameMode<AAetherFrontierMode>())
            if(auto* C=Cast<AAetherCharacter>(Reactive->GetLastSourceActor()))
                if((Service!="TrainingExtinguished"&&!Service.ToString().StartsWith("DailyFire")) || GetOwner()==C)M->Observe(C,Service);
        bWasBurning=false;bExtinguished=true;
    }
}
void AAetherFrontierState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);DOREPLIFETIME(AAetherFrontierState,bSupplyRestored);DOREPLIFETIME(AAetherFrontierState,bWorkshopRestored);
    DOREPLIFETIME(AAetherFrontierState,bBridgeReleased);DOREPLIFETIME(AAetherFrontierState,bPowerOn);DOREPLIFETIME(AAetherFrontierState,ActivityKills);DOREPLIFETIME(AAetherFrontierState,ClosurePhase);
}
AAetherFrontierMode::AAetherFrontierMode()
{
    PrimaryActorTick.bCanEverTick=true;DefaultPawnClass=AAetherFrontierCharacter::StaticClass();
    PlayerStateClass=AAetherPlayerState::StaticClass();HUDClass=AAetherFrontierHUD::StaticClass();GameStateClass=AAetherFrontierState::StaticClass();
}
void AAetherFrontierMode::InitGame(const FString& Map,const FString& Options,FString& Error)
{
    Super::InitGame(Map,Options,Error);bSmoke=FParse::Param(FCommandLine::Get(),TEXT("AetherV4Smoke"));
    if(bSmoke)SavePrefix=TEXT("AetherFrontier_Automation");
    FString Override; if(FParse::Value(FCommandLine::Get(),TEXT("AetherSavePrefix="),Override) && Override.Len()<64 && !Override.Contains("/")&&!Override.Contains("\\"))SavePrefix=Override;
    if(!FAetherRules::Get().bValid){Error=FAetherRules::Get().Error;return;}
    const auto* Content=UAetherGameContent::Load(true);
    if(!Content||!AetherInventory::ValidateCatalog(FAetherRules::Get(),Content->EquipmentCatalog)){Error=TEXT("Item capabilities disagree with equipment catalog");return;}
    Database=NewObject<UAetherFrontierSave>(this);
    bool FoundStorage=false,LoadedStorage=false;
    if(!bSmoke)for(int32 I=0;I<2;++I)
    {
        const FString Slot=SavePrefix+FString::FromInt(I);
        const bool Exists=UGameplayStatics::DoesSaveGameExist(Slot,0);FoundStorage|=Exists;if(!Exists)continue;
        if(auto* Saved=Cast<UAetherFrontierSave>(UGameplayStatics::LoadGameFromSlot(SavePrefix+FString::FromInt(I),0)))
        {
            bool ChecksumValid=Saved->Version==4;
            if(Saved->Version==5)
            {
                TArray<uint8> Bytes;FString Checksum;
                const FString Base=FPaths::ProjectSavedDir()/TEXT("SaveGames")/Slot;
                ChecksumValid=FFileHelper::LoadFileToArray(Bytes,*(Base+TEXT(".sav")))&&FFileHelper::LoadFileToString(Checksum,*(Base+TEXT(".crc")))
                    &&Checksum==FString::Printf(TEXT("%d:%u"),Saved->Generation,FCrc::MemCrc32(Bytes.GetData(),Bytes.Num()));
            }
            bool Valid=Saved->ValidateWorldLedger()&&ChecksumValid&&(Saved->Version==4||Saved->Version==5) && Saved->Generation>=0 && Saved->Profiles.Num()<=128;
            TSet<FString> IDs;for(const auto& P:Saved->Profiles){Valid &= P.Validate()&&!P.CharacterId.IsEmpty()&&!IDs.Contains(P.CharacterId);IDs.Add(P.CharacterId);}
            if(Valid&&Saved->Generation>Database->Generation){LoadedStorage=true;if(Saved->Version==4){Saved->World.Reset();Saved->Version=5;}Database=Saved;}
        }
    }
    if(FoundStorage&&!LoadedStorage)Error=TEXT("Both profile generations are invalid. Preserve Saved/SaveGames and restore a backup; refusing to start with empty profiles.");
}
FString AAetherFrontierMode::InitNewPlayer(APlayerController* PC,const FUniqueNetIdRepl& Id,const FString& Options,const FString& Portal)
{
    FString Error=Super::InitNewPlayer(PC,Id,Options,Portal);if(!Error.IsEmpty())return Error;
    if(GetNumPlayers()>4)return TEXT("Prototype has four player slots.");
    Companions.RemoveAll([](const auto& B){return !IsValid(B);});
    if(Companions.Num()+GetNumPlayers()>4)return TEXT("Four occupied human/AI seats; dismiss a companion safely at town before joining.");
    FString Key=UGameplayStatics::ParseOption(Options,TEXT("DevProfile"));
    if(Key.IsEmpty())Key=GetNetMode()==NM_Standalone?TEXT("LocalPlayer"):FGuid::NewGuid().ToString(EGuidFormats::Digits);
    if(Key.Len()>32)return TEXT("Invalid development profile.");
    for(TCHAR Ch:Key)if(!FChar::IsAlnum(Ch)&&Ch!='_')return TEXT("Invalid development profile.");
    for(TActorIterator<AAetherPlayerState> It(GetWorld());It;++It)if(*It!=PC->PlayerState&&It->Profile.CharacterId==Key)return TEXT("Profile already connected.");
    auto* PS=PC->GetPlayerState<AAetherPlayerState>();if(!PS)return TEXT("Missing profile state.");
    if(auto* Existing=Database->Profiles.FindByPredicate([&](const auto& P){return P.CharacterId==Key;}))PS->Profile=*Existing;
    else {PS->Profile.CharacterId=Key;PS->Profile.Add("Potion",2);
#if !UE_BUILD_SHIPPING
        if(FParse::Param(FCommandLine::Get(),TEXT("AetherV802Net"))&&PS->Profile.Add("SurveySword",1))PS->Profile.Equip(PS->Profile.Inventory.Last().InstanceId);
#endif
    }
    PS->DisplayName=Key;PS->PartyLeader=Key;
    return FString();
}
bool AAetherFrontierMode::WriteDatabase(UAetherFrontierSave* Next)
{
    if(bFailWrites || !Next || !Next->ValidateWorldLedger() || Next->Profiles.Num()>128 || Database->Generation==MAX_int32)return false;
    for(const auto& P:Next->Profiles)if(!P.Validate())return false;
    if(Encounters){Next->Abbey=Encounters->Abbey;Next->Relay=Encounters->Relay;}
    Next->Generation=Database->Generation+1;
    // Alternate generations. Publish live state only after the new generation is readable.
    const FString Slot=SavePrefix+FString::FromInt(Next->Generation%2);
    const FString Base=FPaths::ProjectSavedDir()/TEXT("SaveGames")/Slot;
    // Invalidate only the inactive generation first. A failed data write must not leave
    // a stale commit marker that could authorize this candidate on the next startup.
    if(!IFileManager::Get().Delete(*(Base+TEXT(".crc")),false,true,true))return false;
    if(!UGameplayStatics::SaveGameToSlot(Next,Slot,0)||bFailAfterDataWrite)return false;
    auto* Verify=Cast<UAetherFrontierSave>(UGameplayStatics::LoadGameFromSlot(Slot,0));
    if(!Verify||Verify->Generation!=Next->Generation||!Verify->ValidateWorldLedger())return false;
    TArray<uint8> Bytes;
    if(!FFileHelper::LoadFileToArray(Bytes,*(Base+TEXT(".sav"))))return false;
    const FString Checksum=FString::Printf(TEXT("%d:%u"),Next->Generation,FCrc::MemCrc32(Bytes.GetData(),Bytes.Num()));
    if(!FFileHelper::SaveStringToFile(Checksum,*(Base+TEXT(".crc.pending")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))return false;
    if(!IFileManager::Get().Move(*(Base+TEXT(".crc")),*(Base+TEXT(".crc.pending")),true,true))return false;
    Database=Next;return true;
}
bool AAetherFrontierMode::Commit(AAetherPlayerState* PS,FAetherProfile Next,FName WorldFact,FName FactSource)
{
    if(!PS || !PS->HasAuthority() || Next.CharacterId!=PS->Profile.CharacterId || Next.Revision==MAX_int32 || Next.Revision!=PS->Profile.Revision || !Next.Validate())return false;
    auto* Candidate=DuplicateObject<UAetherFrontierSave>(Database,this);
    CollectPublicFacts(Candidate->WorldFacts);
    if(!WorldFact.IsNone())
    {
        const auto* Rule=FAetherRules::Get().Objectives.Find(WorldFact);
        if(!Rule||Rule->Scope!=EAetherObjectiveScope::World||!Rule->FactSources.Contains(FactSource))return false;
        Candidate->WorldFacts.Record(WorldFact,FactSource);
    }
    if(!Candidate->WorldFacts.Sources.OrderIndependentCompareEqual(Database->WorldFacts.Sources)&&!CaptureWorldCandidate(Candidate))return false;
    AetherQuests::Settle(Next,Candidate->WorldFacts,false);
    ++Next.Revision;
    if(auto* P=Candidate->Profiles.FindByPredicate([&](const auto& V){return V.CharacterId==Next.CharacterId;}))*P=Next;else Candidate->Profiles.Add(Next);
    if(!WriteDatabase(Candidate))return false;
    PS->Profile=MoveTemp(Next);PS->ForceNetUpdate();
    if(PS->Profile.Claims.Contains(FName("Q_Main_03")))
     for(TActorIterator<AAetherFrontierCharacter> It(GetWorld());It;++It)if(It->Reactive->bOwnerOnlyStimuli&&It->GetOwner()==PS->GetPawn())It->Pacify();
    return true;
}
bool AAetherFrontierMode::CaptureWorldCandidate(UAetherFrontierSave* Candidate) const
{
    if(!Candidate)return false;
    if(!GetWorld()->GetSubsystem<UReactiveWorldSubsystem>()->Capture(Candidate->World))return false;
    auto* S=GetGameState<AAetherFrontierState>();Candidate->bWorkshopRestored=S->bWorkshopRestored;Candidate->bSupplyRestored=S->bSupplyRestored;Candidate->bBridgeReleased=S->bBridgeReleased;Candidate->bPowerOn=S->bPowerOn;
    const auto& Environment=GetWorld()->GetSubsystem<UReactiveWorldSubsystem>()->GetSimulation()->GetEnvironment();Candidate->AmbientTemperatureC=Environment.TemperatureC;Candidate->RainKgPerM2Sec=Environment.RainKgPerM2Sec;Candidate->WindMPerSec=Environment.WindMPerSec;
    if(Encounters){Candidate->Abbey=Encounters->Abbey;Candidate->Relay=Encounters->Relay;}
    CollectPublicFacts(Candidate->WorldFacts);
    return true;
}
bool AAetherFrontierMode::SaveWorld()
{
    auto* Candidate=DuplicateObject<UAetherFrontierSave>(Database,this);
    return CaptureWorldCandidate(Candidate)&&WriteDatabase(Candidate);
}
void AAetherFrontierMode::Observe(AAetherCharacter* C,FName Fact)
{
    auto* FC=Cast<AAetherFrontierCharacter>(C);if(FC&&FC->CompanionOwner)FC=FC->CompanionOwner;
    auto* PS=FC?FC->ProfileState():nullptr;if(!PS)return;
    auto Next=PS->Profile;
    if(Fact.ToString().StartsWith("DailyFire")&&Next.Claims.Contains(FName("Q_Main_08")))
    {Next.RefreshDaily(FDateTime::UtcNow().ToString(TEXT("%Y%m%d")));Next.DailyEvidence.AddUnique(Fact);Commit(PS,Next);return;}
    if(!Next.Observe(Fact))return;
    AetherQuests::Settle(Next,Database->WorldFacts,false);
    if(Commit(PS,Next))FC->Notify(TEXT("Objective recorded. Completed quest rewards saved."));
    else FC->Notify(TEXT("Storage unavailable; objective was not committed. Retry the interaction."));
}
void AAetherFrontierMode::RestartPlayer(AController* C)
{
    auto* PS=C?C->GetPlayerState<AAetherPlayerState>():nullptr;
    const FVector P=FParse::Param(FCommandLine::Get(),TEXT("AetherV4NetServer"))?FVector(-6500,-28700,120+GetNumPlayers()*10):PS&&PS->Profile.bRegistered?FVector(-500,-500,120):FVector(-6500,-29000,120);
    const bool Closure=FParse::Param(FCommandLine::Get(),TEXT("AetherV807Server"));
    const bool Capture=FParse::Param(FCommandLine::Get(),TEXT("AetherV4Capture"));
    RestartPlayerAtTransform(C,FTransform(FRotator(0,90,0),Closure?FVector(4900,5100,110):Capture?FVector(1100,-1700,120):P));if(C)C->SetControlRotation(FRotator(-8,Capture?120:90,0));
}
AAetherFrontierProp* AAetherFrontierMode::Make(FName Id,FName Service,FVector P,FVector Scale,EAetherObjectKind Kind,const FString& Label)
{
    if(GetWorld()->GetWorldSettings()->ActorHasTag("AetherPartitionShell")&&UAetherWorldAuthoring::IsShellPiece(Id))return nullptr;
    auto* A=GetWorld()->SpawnActorDeferred<AAetherFrontierProp>(AAetherFrontierProp::StaticClass(),FTransform(P),nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    A->Spec.Id=Id;A->Spec.Kind=Kind;A->Spec.Scale=Scale;A->Spec.Label=Label;A->Service=Service;
    A->Spec.Color=Kind==EAetherObjectKind::Water?FLinearColor(.03,.25,.5):Kind==EAetherObjectKind::Rope||Kind==EAetherObjectKind::Timber?FLinearColor(.45,.23,.08):FLinearColor(.34,.39,.44);
    A->Spec.bInteractiveMaterial=Kind==EAetherObjectKind::Cistern||Kind==EAetherObjectKind::Water||Kind==EAetherObjectKind::Rope||Kind==EAetherObjectKind::Timber;
    A->ConfigureMaterial();
    if(Service=="Conductor"||Service=="Source"||Service=="Receiver")
    {A->Reactive->bParticipatesInSimulation=true;A->Reactive->Preset=EReactiveMaterialPreset::Metal;A->Reactive->InteractionRadiusCm=220;A->Spec.bInteractiveMaterial=true;A->Spec.Color=FLinearColor(.65,.7,.8);}
    if(Service=="Source"){A->Reactive->ReceiverLoad=0;A->Mechanism->PowerW=1000;A->Mechanism->RemainingEnergyJ=3600000;}
    if(Service=="Receiver"){A->Reactive->ReceiverLoad=4;A->Reactive->ReceiverCapacityJ=500;A->Reactive->ElectricalHeatFraction=.05;A->Reactive->bElectricalTerminal=true;}
    A->Traversal->bAuthoredBridge=Service=="Bridge";
    A->Mechanism->bBuoyant=Service=="Crate";
    A->Mechanism->bReportImpacts=Service=="Bridge"||Service=="Conductor"||Service=="Crate";
    if(Service=="Bridge"||Service=="HingedGate"){A->Reactive->bParticipatesInSimulation=true;A->Spec.bInteractiveMaterial=true;}
    A->bCarryable=Service=="Conductor"||Service=="Crate";A->Reactive->bTrackMovement=A->bCarryable||Service=="Bridge"||Service=="HingedGate";
    if(Kind==EAetherObjectKind::Water)A->Reactive->InitialWaterKg=.5;
    if(Kind==EAetherObjectKind::Cistern)A->Reactive->InitialWaterKg=8;
    if(const auto* Rule=FAetherRules::Get().Objectives.Find(Service))A->bInspectableFire=Rule->bInspectableFire;
    if(A->bInspectableFire||AetherGuide::IsPersonalFire(Service))
    {
        auto* M=NewObject<UReactiveMaterialAsset>(A);M->Parameters.InitialFuelKg=10;A->Reactive->MaterialAsset=M;
    }
    if(AetherGuide::IsPersonalFire(Service)){A->Reactive->StableId=NAME_None;A->Reactive->bOwnerOnlyStimuli=true;}
    if(Id.ToString().StartsWith("Roof")){A->Spec.bInteractiveMaterial=false;A->Reactive->bParticipatesInSimulation=false;}
    A->bAcceptsWater=A->Reactive->bParticipatesInSimulation&&A->Reactive->GetMaterial().WaterCapacityKg>0;
    UGameplayStatics::FinishSpawningActor(A,FTransform(P));Props.Add(A);return A;
}
AAetherFrontierProp* AAetherFrontierMode::Prop(FName Id) const
{for(AAetherFrontierProp* P:Props)if(IsValid(P)&&P->Spec.Id==Id)return P;return nullptr;}
AAetherFrontierCharacter* AAetherFrontierMode::SpawnFighter(FVector P,EAetherFighter Type,FName Id)
{
    auto* C=GetWorld()->SpawnActorDeferred<AAetherFrontierCharacter>(AAetherFrontierCharacter::StaticClass(),FTransform(P),nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
    C->Fighter=Type;C->Reactive->StableId=NAME_None;UGameplayStatics::FinishSpawningActor(C,FTransform(P));return C;
}
void AAetherFrontierMode::BuildWorld()
{
    Make("Ground",NAME_None,{0,0,-400},{800,800,2},EAetherObjectKind::Stone,TEXT(""));
    Make("Town",NAME_None,{0,0,-60},{200,200,1.2},EAetherObjectKind::Stone,TEXT("EMBERHEARTH / SERVICES"));
    Make("SouthRoad",NAME_None,{-6500,-19000,-50},{16,220,1},EAetherObjectKind::Stone,TEXT(""));
    Make("WestRoad",NAME_None,{-18000,0,-50},{180,16,1},EAetherObjectKind::Stone,TEXT(""));
    Make("EastRoad",NAME_None,{18000,0,-50},{180,16,1},EAetherObjectKind::Stone,TEXT(""));
    Make("NorthRoad",NAME_None,{0,18000,-50},{16,180,1},EAetherObjectKind::Stone,TEXT(""));
    Make("Start",NAME_None,{-6500,-29000,200},{.1,.1,.1},EAetherObjectKind::Stone,TEXT("EMBER FRONTIER / WASD + MOUSE / E INTERACT\nCollect both supplies, then follow the road NORTH to town"));
    Make("SupplyA","SupplyA",{-6350,-28700,40},{.7,.7,.8},EAetherObjectKind::Stone,TEXT("E / SUPPLY A"));
    Make("SupplyB","SupplyB",{-6650,-28300,40},{.7,.7,.8},EAetherObjectKind::Stone,TEXT("E / SUPPLY B"));
    Make("Gate","Gate",{-6500,-9800,160},{.2,.2,3.2},EAetherObjectKind::Stone,TEXT("E / SOUTH GATE\nRegistrar and inn: town centre"));
    Make("Registrar","Register",{-600,-700,90},{.5,.5,1.8},EAetherObjectKind::Stone,TEXT("E / REGISTER / LAN TING"));
    Make("Inn","Inn",{-400,-300,90},{.5,.5,1.8},EAetherObjectKind::Stone,TEXT("E / INN / BIND CHECKPOINT"));
    Make("Teacher","Teacher",{400,-300,90},{.5,.5,1.8},EAetherObjectKind::Stone,TEXT("E / TEACHER / LEARN & TRAIN\nR equipment / hold LMB heavy / RMB guard"));
    Make("Dummy","Dummy",{850,-300,90},{.7,.7,1.8},EAetherObjectKind::Stone,TEXT("TRAINING DUMMY / 3 HITS"));
    Make("Shop","Shop",{-400,300,90},{.5,.5,1.8},EAetherObjectKind::Stone,TEXT("E / POTION 20 GOLD / I MORE SHOP ACTIONS"));
    Make("Recruit","Recruit",{400,300,90},{.5,.5,1.8},EAetherObjectKind::Stone,TEXT("E / RECRUIT / AFTER BOTH FIELD QUESTS"));
    Make("Steward","SealDelivered",{0,700,90},{.5,.5,1.8},EAetherObjectKind::Stone,TEXT("E / RETURN ANCIENT SEAL"));
    Make("DailyBoard","Daily",{-900,300,100},{.2,2,2},EAetherObjectKind::Stone,TEXT("E / DAILY SUPPLY / 2 SUPPLIES -> 30 GOLD"));
    Make("PatrolBoard","DailyPatrol",{-900,600,100},{.2,2,2},EAetherObjectKind::Stone,TEXT("E / DAILY PATROL / THREE MARKERS"));
    Make("FireBoard","DailyFire",{-900,900,100},{.2,2,2},EAetherObjectKind::Stone,TEXT("E / DAILY FIRE COMMISSION"));
    for(int32 I=0;I<3;++I)
    {
        FVector Loc=I==0?FVector(-27000,-1200,80):I==1?FVector(26400,-1200,80):FVector(0,24000,80);
        Make(*FString::Printf(TEXT("Patrol%d"),I),*FString::Printf(TEXT("Patrol%d"),I),Loc,{.5,.5,1.6},EAetherObjectKind::Stone,TEXT("E / PATROL MARKER"));
        Make(*FString::Printf(TEXT("SupplyCache%d"),I),"Gather",Loc+FVector(180,0,-40),{.6,.6,.8},EAetherObjectKind::Stone,TEXT("E / DAILY SUPPLY CACHE"));
    }
    Make("Well","Well",{950,300,50},{1,1,1},EAetherObjectKind::Cistern,TEXT("E / FINITE WATER RESERVE"));
    for(int32 I=0;I<6;++I)
    {
        const float X=(I%3-1)*1900.f;const float Y=I<3?-1800.f:1800.f;
        Make(*FString::Printf(TEXT("House%d"),I),NAME_None,{X,Y,220},{10,8,4.4},EAetherObjectKind::Stone,TEXT(""));
        Make(*FString::Printf(TEXT("Roof%d"),I),NAME_None,{X,Y,480},{11,9,.8},EAetherObjectKind::Timber,TEXT(""));
    }
    Make("ForestFloor",NAME_None,{-27000,0,-60},{80,100,1.2},EAetherObjectKind::Stone,TEXT("ASHWOOD / EXTINGUISH FIRES AND RESCUE"));
    for(int32 I=0;I<3;++I)
    {
        FName Id=*FString::Printf(TEXT("ForestFire%d"),I);
        auto* F=Make(Id,Id,{-26500.f-I*450,350,50},{.9,.9,1},EAetherObjectKind::Timber,TEXT("FIRE / WATER OR LOCAL RESERVOIR"));
        FReactiveStimulus H;H.HeatJ=60000;F->Reactive->Inject(H);
        Make(*FString::Printf(TEXT("Bucket%d"),I),"Bucket",{-26500.f-I*450,650,45},{.6,.6,.9},EAetherObjectKind::Water,TEXT("E / POUR FINITE WATER"));
    }
    Make("Rescue","Rescue",{-27700,-300,90},{.5,.5,1.8},EAetherObjectKind::Stone,TEXT("E / RESCUE CRAFTSMAN"));
    for(int32 I=0;I<16;++I)Make(*FString::Printf(TEXT("Tree%d"),I),NAME_None,{-30000.f+(I%4)*1800, -3400.f+(I/4)*1800,220},{.8,.8,4.4},EAetherObjectKind::Timber,TEXT(""));
    Make("WorksWest",NAME_None,{26500,0,-60},{10,24,1.2},EAetherObjectKind::Stone,TEXT("OLD WATERWORKS / THREE ROUTES"));
    Make("WorksEast",NAME_None,{28200,0,-60},{16,24,1.2},EAetherObjectKind::Stone,TEXT(""));
    Make("Maintenance",NAME_None,{27350,1000,-20},{9,3,.4},EAetherObjectKind::Stone,TEXT("MAINTENANCE BYPASS / NO SPELL REQUIRED"));
    auto* Rope=Make("WorksRope","Support",{26900,-650,90},{.2,.2,1.8},EAetherObjectKind::Rope,TEXT("CUT OR BURN ROPE / RELEASE BRIDGE"));
    Rope->Reactive->bTrackMovement=false;
    auto* Bridge=Make("WorksBridge","Bridge",{27350,-600,450},{9,3,.25},EAetherObjectKind::Stone,TEXT(""));Bridge->Mesh->SetMassOverrideInKg(NAME_None,60,true);Bridge->Mechanism->Supports.Add(Rope->Reactive);
    Make("BridgeStopA",NAME_None,{26970,-600,-30},{.4,3,.6},EAetherObjectKind::Stone,TEXT(""));
    Make("BridgeStopB",NAME_None,{27730,-600,-30},{.4,3,.6},EAetherObjectKind::Stone,TEXT(""));
    for(int32 I=0;I<3;++I)Make(*FString::Printf(TEXT("WorksWater%d"),I),"Water",{27050.f+I*300,0,-12},{3,3,.2},EAetherObjectKind::Water,I==0?TEXT("FROST / FREEZE SHALLOW CHANNEL"):TEXT(""));
    // Explicit adjacent surface ports; buckets and unrelated wet objects never auto-connect.
    for(int32 I=0;I<2;++I)
    {
        auto* West=Prop(*FString::Printf(TEXT("WorksWater%d"),I));auto* East=Prop(*FString::Printf(TEXT("WorksWater%d"),I+1));
        FReactiveLiquidPort Right;Right.PortId="East";Right.TargetStableId=East->Reactive->StableId;
        Right.LocalPositionCm=FVector(50,0,0);Right.TargetLocalPositionCm=FVector(-50,0,0);Right.MaxKgPerSecond=.1;
        West->Reactive->LiquidPorts.Add(Right);
        FReactiveLiquidPort Left=Right;Left.PortId="West";Left.TargetStableId=West->Reactive->StableId;
        Left.LocalPositionCm=FVector(-50,0,0);Left.TargetLocalPositionCm=FVector(50,0,0);East->Reactive->LiquidPorts.Add(Left);
    }
    Make("Pump","SupplyRestored",{28100,0,80},{1,1,1.6},EAetherObjectKind::Stone,TEXT("E / RESTORE SUPPLY / OR INSPECT COMPLETED WORK"));
    Make("PowerSource","Source",{28200,650,40},{1,1,.8},EAetherObjectKind::Stone,TEXT("E / FIXED POWER SOURCE ON-OFF"))->bGlobalPowerService=true;
    Make("PowerReceiver","Receiver",{28500,650,40},{1,1,.8},EAetherObjectKind::Stone,TEXT("E / POWERED PUMP / MOVE ROD TO CONNECT"));
    Make("MetalRod","Conductor",{28350,350,40},{2.1,.3,.3},EAetherObjectKind::Stone,TEXT("G / CARRY-DROP CONDUCTOR / V PUSH"));
    Make("Crate","Crate",{26500,-700,50},{.8,.8,1},EAetherObjectKind::Timber,TEXT("G / CARRY WOODEN CRATE"));
    auto* Anchor=Make("GateAnchor",NAME_None,{28500,-650,120},{.4,.4,2.4},EAetherObjectKind::Stone,TEXT(""));
    auto* Gate=Make("HingedGate","HingedGate",{28650,-650,100},{3,.2,2},EAetherObjectKind::Stone,TEXT("E / BOUNDED HINGE MOTOR / BLOCK WITH CRATE"));
    Gate->Mesh->SetSimulatePhysics(true);Gate->Mesh->SetMassOverrideInKg(NAME_None,35,true);
    auto* Joint=NewObject<UPhysicsConstraintComponent>(Gate);Gate->AddInstanceComponent(Joint);Joint->RegisterComponent();Joint->SetWorldLocation(Anchor->GetActorLocation());
    Joint->SetDisableCollision(true);Joint->SetLinearXLimit(LCM_Locked,0);Joint->SetLinearYLimit(LCM_Locked,0);Joint->SetLinearZLimit(LCM_Locked,0);
    Joint->SetAngularSwing1Limit(ACM_Limited,80);Joint->SetAngularSwing2Limit(ACM_Locked,0);Joint->SetAngularTwistLimit(ACM_Locked,0);
    Joint->SetAngularDriveMode(EAngularDriveMode::TwistAndSwing);Joint->SetAngularOrientationDrive(true,false);Joint->SetAngularDriveParams(5000,500,20000);
    Joint->SetConstrainedComponents(Anchor->Mesh,NAME_None,Gate->Mesh,NAME_None);Gate->Mechanism->Constraint=Joint;
    Make("AbbeyFloor",NAME_None,{0,27000,-60},{70,80,1.2},EAetherObjectKind::Stone,TEXT("BROKEN BELL ABBEY"));
    Make("AbbeyEntry","Abbey",{0,24400,100},{1,1,2},EAetherObjectKind::Stone,TEXT("E / START PARTY ENCOUNTER"));
    Make("AbbeyValve","AbbeyValve",{0,25500,90},{.5,.5,1.8},EAetherObjectKind::Stone,TEXT("E / CHANNEL VALVE / P + H COMMAND COMPANION"));
    Make("AbbeyExit","GuardianDefeated",{0,28200,100},{1,1,2},EAetherObjectKind::Stone,TEXT("E / CLAIM ENCOUNTER PROOF"));
    for(int32 I=0;I<8;++I)for(int32 Side:{-1,1})Make(*FString::Printf(TEXT("Pillar%d_%d"),I,Side),NAME_None,{Side*2200.f,24600.f+I*700,250},{1.2,1.2,5},EAetherObjectKind::Stone,TEXT(""));
    Make("ActivityFloor",NAME_None,{25000,22000,-60},{50,50,1.2},EAetherObjectKind::Stone,TEXT("RELAY DEFENCE"));
    Make("Activity","Activity",{25000,22000,100},{1,1,2},EAetherObjectKind::Stone,TEXT("E / PUBLIC DEFENCE / START OR CLAIM"));
    Make("RingEast",NAME_None,{25000,12000,-50},{16,220,1},EAetherObjectKind::Stone,TEXT(""));
    Make("RingNorth",NAME_None,{12500,26000,-50},{250,16,1},EAetherObjectKind::Stone,TEXT(""));
}
void AAetherFrontierMode::BeginPlay()
{
    Super::BeginPlay();BuildWorld();BuildWorkshop();
    for(const auto& Loot:Database->Loot)if(Loot.ClaimedBy.IsEmpty())SpawnLoot(Loot);
    Encounters=GetWorld()->SpawnActor<AAetherEncounterDirector>();
    if(Database->Abbey.Phase==EAetherEncounterPhase::Succeeded)Encounters->Abbey=Database->Abbey;
    if(Database->Relay.Phase==EAetherEncounterPhase::Succeeded)Encounters->Relay=Database->Relay;
    auto* S=GetGameState<AAetherFrontierState>();S->bWorkshopRestored=Database->bWorkshopRestored;S->bSupplyRestored=Database->bSupplyRestored;S->bBridgeReleased=Database->bBridgeReleased;S->bPowerOn=Database->bPowerOn;
    if(!Database->World.IsEmpty()&&!GetWorld()->GetSubsystem<UReactiveWorldSubsystem>()->Restore(Database->World))UE_LOG(LogTemp,Warning,TEXT("V4 world snapshot incompatible; profile records retained."));
    if(!Database->World.ContainsByPredicate([](const auto& R){return R.StableId=="LabFire";})){FReactiveStimulus H;H.HeatJ=60000;Prop("LabFire")->Reactive->Inject(H);}
    for(AAetherFrontierProp* P:Props)if(P->bCarryable)P->Mesh->SetSimulatePhysics(true);
    Prop("PowerSource")->Mechanism->bPowerEnabled=S->bPowerOn;
    GetWorld()->GetSubsystem<UReactiveWorldSubsystem>()->SetWeather(Database->AmbientTemperatureC,Database->RainKgPerM2Sec,Database->WindMPerSec);S->bRain=Database->RainKgPerM2Sec>0;
    UE_LOG(LogTemp,Display,TEXT("AETHER_V4_READY engine=%s props=%d"),*FEngineVersion::Current().ToString(),Props.Num());
}
void AAetherFrontierMode::Logout(AController* C)
{
    auto* Pawn=C?Cast<AAetherFrontierCharacter>(C->GetPawn()):nullptr;
    if(Pawn)Pawn->ReleaseCarry();
    if(C)LeaveParty(C->GetPlayerState<AAetherPlayerState>());
    for(TActorIterator<AAetherFrontierCharacter> It(GetWorld());It;++It)if(It->Reactive->bOwnerOnlyStimuli&&It->GetOwner()==Pawn)It->Destroy();
    for(AAetherFrontierCharacter* Buddy:Companions)if(IsValid(Buddy)&&Buddy->CompanionOwner==Pawn)Buddy->Destroy();
    for(int32 I=Props.Num()-1;I>=0;--I)if(IsValid(Props[I])&&Props[I]->GetOwner()==Pawn&&Props[I]->Reactive->StableId.IsNone()){Props[I]->Destroy();Props.RemoveAt(I);}
    Super::Logout(C);
}
void AAetherFrontierMode::CreditHit(AAetherCharacter* Target,AAetherCharacter* Source)
{
    auto* C=Cast<AAetherFrontierCharacter>(Source);if(C&&C->CompanionOwner)C=C->CompanionOwner;
    if(C&&C->ProfileState()&&Target)KillCredit.FindOrAdd(Target).Add(*C->ProfileState()->Profile.CharacterId);
}
void AAetherFrontierMode::Tick(float Dt)
{
    Super::Tick(Dt);Elapsed+=Dt;SaveTimer+=Dt;PowerTimer+=Dt;WeatherTimer+=Dt;AreaTimer+=Dt;
    auto* S=GetGameState<AAetherFrontierState>();
    if(WeatherTimer>=1)
    {
        auto* W=GetWorld()->GetSubsystem<UReactiveWorldSubsystem>();const auto& E=W->GetSimulation()->GetEnvironment();
        const double Target=S->bRain?.003:0;const double Rain=FMath::FInterpConstantTo(E.RainKgPerM2Sec,Target,WeatherTimer,.001);
        if(!FMath::IsNearlyEqual(E.RainKgPerM2Sec,Rain,1.e-8))W->SetWeather(20,Rain,FVector(2,0,0));WeatherTimer=0;
    }
    if(AreaTimer>=2)
    {
        for(auto It=KillCredit.CreateIterator();It;++It)if(!It.Key().IsValid())It.RemoveCurrent();
        RefreshWorldProgress();
        AreaTimer=0;TArray<FVector> Players;for(TActorIterator<AAetherFrontierCharacter> It(GetWorld());It;++It)if(It->ProfileState())Players.Add(It->GetActorLocation());
        for(TActorIterator<AAetherFrontierCharacter> It(GetWorld());It;++It)if(It->Fighter!=EAetherFighter::Player)
        {bool Near=false;for(auto P:Players)Near|=FVector::DistSquared(P,It->GetActorLocation())<FMath::Square(7000.);It->SetActorTickInterval(Near?0.f:1.f);}
        for(const auto& P:Props)if(IsValid(P))
        {bool Near=false;for(auto V:Players)Near|=FVector::DistSquared(V,P->GetActorLocation())<FMath::Square(7000.);P->SetActorTickInterval(Near?.1f:1.f);}
    }
    if(FParse::Param(FCommandLine::Get(),TEXT("AetherV4NetServer"))){S->bSupplyRestored=true;S->bPowerOn=false;}
    if(!S->bBridgeReleased&&Prop("WorksRope")->Reactive->State.bBroken)
    {S->bBridgeReleased=true;Prop("WorksBridge")->Mesh->SetSimulatePhysics(true);Prop("WorksBridge")->ForceNetUpdate();}
    Prop("PowerSource")->Mechanism->bPowerEnabled=S->bPowerOn;
    if(SaveTimer>10){if(SaveWorld())SaveTimer=0;}
    for(TActorIterator<AAetherFrontierCharacter> It(GetWorld());It;++It)
    {
        if(It->Fighter!=EAetherFighter::Player&&!It->Alive())
        {
            for(auto Name:KillCredit.FindRef(*It))for(TActorIterator<AAetherPlayerState> PS(GetWorld());PS;++PS)if(PS->Profile.CharacterId==Name.ToString())
            {
                auto* C=Cast<AAetherFrontierCharacter>(PS->GetPawn());if(*It==Guardian)Observe(C,"GuardianDefeated");
                if(*It==ActivityEnemy)S->ActivityKills=1;
            }
        }
        if(It->ProfileState()&&It->GetActorLocation().Z < -600)It->SetActorLocation(It->ProfileState()->Profile.bRegistered?FVector(-500,-500,120):FVector(-6500,-29000,120));
    }
    if(FParse::Param(FCommandLine::Get(),TEXT("AetherV5Check"))&&Elapsed>2)
    {
        auto* C=Cast<AAetherFrontierCharacter>(UGameplayStatics::GetPlayerPawn(this,0));
        if(C&&C->ProfileState())
        {
            if(!bLightCheckStarted)
            {bLightCheckStarted=true;S->bPowerOn=false;C->SetActorLocation(FVector(-6500,-28700,120));Interact(C);Interact(C);
             const FGuid Receipt(0xA375E5,0,0,1);RecordCampClear("Trail",Receipt);
             const FName LootId=*FString("Loot_"+Receipt.ToString(EGuidFormats::Digits));if(auto* Loot=Prop(LootId)){C->SetActorLocation(Loot->GetActorLocation()+FVector(0,0,100));ClaimLoot(C,LootId);ClaimLoot(C,LootId);C->SetActorLocation(FVector(-6500,-28700,120));}
            }
            bool NavigationReady=true;const bool Partition=GetWorld()->GetWorldPartition()!=nullptr;
            if(GetWorld()->GetWorldSettings()->ActorHasTag("AetherPartitionShell"))
            {auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());FNavLocation Point;NavigationReady=Partition&&Nav&&Nav->ProjectPointToNavigation(C->GetActorLocation(),Point,FVector(300,300,500));if(!NavigationReady&&Elapsed<7)return;}
            if(SaveWorld())
            {
                const bool Pass=NavigationReady&&C->ProfileState()->Profile.Count("Material")==2&&Database->CampReceipts.Num()==1&&C->AbilitySystem==C->ProfileState()->AbilitySystem&&C->MaxHealth==100&&C->ProfileState()->Profile.Count("Supply")==1
                    &&Database->Version==5&&Database->Generation>0&&Encounters&&Prop("HingedGate")->Mechanism->Constraint&&Prop("WorksBridge")->Mechanism->Supports.Num()==1
                    &&Prop("PowerSource")->Mechanism->RemainingEnergyJ>0&&Prop("PowerSource")->Mechanism->RemainingEnergyJ<3600000&&FAetherRules::Get().bValid;
                UE_LOG(LogTemp,Display,TEXT("AETHER_V5_LIGHT_%s bodies=%d generation=%d source=%.1f partition=%d nav=%d"),Pass?TEXT("PASS"):TEXT("FAIL"),GetWorld()->GetSubsystem<UReactiveWorldSubsystem>()->GetSimulation()->GetStats().Registered,Database->Generation,Prop("PowerSource")->Mechanism->RemainingEnergyJ,Partition,NavigationReady);
                FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
            }
        }
        if(Elapsed>8){UE_LOG(LogTemp,Error,TEXT("AETHER_V5_LIGHT_FAIL timeout"));FPlatformMisc::RequestExitWithStatus(false,1);}
    }
    if(FParse::Param(FCommandLine::Get(),TEXT("AetherAnimationCheck"))&&Elapsed>2)CheckAnimation();
    if(FParse::Param(FCommandLine::Get(),TEXT("AetherGuidanceCheck"))&&Elapsed>2)CheckGuidance();
    if(FParse::Param(FCommandLine::Get(),TEXT("AetherReactionCheck"))&&Elapsed>2)CheckReactions();
    if(FParse::Param(FCommandLine::Get(),TEXT("AetherServiceCheck"))&&Elapsed>2)CheckServices();
    if(FParse::Param(FCommandLine::Get(),TEXT("AetherDataCheck"))&&Elapsed>2)CheckDataContracts();
    if(bSmoke)SmokeStep();
    if(FParse::Param(FCommandLine::Get(),TEXT("AetherV807Server")))CheckClosure();
    if(FParse::Param(FCommandLine::Get(),TEXT("AetherV806Capture")))CaptureWorkshop();
    if(FParse::Param(FCommandLine::Get(),TEXT("AetherV4Capture")))
    { static bool Taken=false;if(Elapsed>8&&!Taken){Taken=true;FScreenshotRequest::RequestScreenshot(FPaths::ProjectDir()/TEXT("Docs/Images/AetherFrontier.png"),true,false);}if(Elapsed>11)FPlatformMisc::RequestExit(false); }
}

bool AAetherFrontierMode::CommitOffline(FAetherProfile Next)
{
 if(!Next.Validate()||Next.Revision==MAX_int32)return false;
 for(TActorIterator<AAetherPlayerState> It(GetWorld());It;++It)if(It->Profile.CharacterId==Next.CharacterId)return false;
 const auto* Existing=Database->Profiles.FindByPredicate([&](const auto& P){return P.CharacterId==Next.CharacterId;});
 if(!Existing||Existing->Revision!=Next.Revision)return false;
 auto* Candidate=DuplicateObject<UAetherFrontierSave>(Database,this);AetherQuests::Settle(Next,Candidate->WorldFacts,false);++Next.Revision;
 for(auto& P:Candidate->Profiles)if(P.CharacterId==Next.CharacterId)P=Next;
 return WriteDatabase(Candidate);
}

bool AAetherFrontierMode::CanChangeParty(const AAetherFrontierCharacter* C) const
{
 if(!C||!C->Alive()||!C->ProfileState()||C->TimeSinceDamage()<8||FVector::DistSquared2D(C->GetActorLocation(),FVector::ZeroVector)>FMath::Square(8000.))return false;
 if(Encounters)for(const auto* Run:{&Encounters->Abbey,&Encounters->Relay})
  if(Run->Phase!=EAetherEncounterPhase::Idle&&Run->Phase!=EAetherEncounterPhase::Failed&&Run->Phase!=EAetherEncounterPhase::Succeeded&&Run->Participants.Contains(C->ProfileState()->Profile.CharacterId))return false;
 for(TActorIterator<AAetherCharacter> It(GetWorld());It;++It)if(It->Fighter!=EAetherFighter::Player&&It->Alive()&&FVector::DistSquared(It->GetActorLocation(),C->GetActorLocation())<FMath::Square(1800.))return false;
 return true;
}
void AAetherFrontierMode::LeaveParty(AAetherPlayerState* PS)
{
 if(!PS)return;const FString Previous=PS->PartyLeader;
 if(Previous==PS->Profile.CharacterId)
 {
  FString Successor;
  for(TActorIterator<AAetherPlayerState> It(GetWorld());It;++It)if(*It!=PS&&It->PartyLeader==Previous&&!It->IsInactive())
   if(Successor.IsEmpty()||It->Profile.CharacterId<Successor)Successor=It->Profile.CharacterId;
  for(TActorIterator<AAetherPlayerState> It(GetWorld());It;++It)
  {if(*It!=PS&&It->PartyLeader==Previous)It->PartyLeader=Successor.IsEmpty()?It->Profile.CharacterId:Successor;if(It->InvitedBy==Previous)It->InvitedBy.Empty();}
 }
 PS->PartyLeader=PS->Profile.CharacterId;PS->InvitedBy.Empty();
}

bool UAetherFrontierSave::ValidateWorldLedger() const
{
 if(Loot.Num()>128||CampReceipts.Num()>32||ServiceReceipts.Num()>64||!WorldFacts.Validate())return false;
 TSet<FGuid> ServiceIDs;
 for(const auto& R:ServiceReceipts)
 {
  const auto* Profile=Profiles.FindByPredicate([&](const auto& P){return P.CharacterId==R.CharacterId;});
  if(!R.Command.Id.IsValid()||ServiceIDs.Contains(R.Command.Id)||R.Command.TargetId.IsNone()||R.Command.TargetId.ToString().Len()>128
      ||R.CharacterId.IsEmpty()||!Profile||R.Command.ExpectedRevision<0||R.Command.ExpectedRevision>=Profile->Revision)return false;
  ServiceIDs.Add(R.Command.Id);
 }
 TSet<FGuid> IDs;TSet<FName> Camps;
 for(const auto& L:Loot){if(!L.ClaimId.IsValid()||IDs.Contains(L.ClaimId)||L.Count<1||L.Count>99||FAetherProfile::MaxStack(L.Definition)==0||L.Location.ContainsNaN()||L.Location.GetAbsMax()>100000||L.ClaimedBy.Len()>32)return false;IDs.Add(L.ClaimId);}
 for(const auto& R:CampReceipts){if(Camps.Contains(R.Definition)||!FAetherRules::Get().Encounters.Contains(R.Definition)||!R.Instance.IsValid()||R.RespawnAfterUtc<0)return false;Camps.Add(R.Definition);}return true;
}
void AAetherFrontierMode::SpawnLoot(const FAetherWorldLoot& Loot)
{
 if(!Loot.ClaimedBy.IsEmpty())return;const FName Id=*FString("Loot_"+Loot.ClaimId.ToString(EGuidFormats::Digits));if(Prop(Id))return;
 Make(Id,"Loot",Loot.Location,FVector(.45,.45,.45),EAetherObjectKind::Stone,TEXT("E / SHARED LOOT / ONE CLAIM"));
}
bool AAetherFrontierMode::RecordCampClear(FName Definition,FGuid Instance)
{
 if(!Instance.IsValid())return false;const auto* Rule=FAetherRules::Get().Encounters.Find(Definition);if(!Rule||Rule->RespawnSeconds<=0)return false;
 if(Database->CampReceipts.ContainsByPredicate([&](const auto& R){return R.Instance==Instance;}))return true;
 auto* Next=DuplicateObject<UAetherFrontierSave>(Database,this);Next->Loot.RemoveAll([](const auto& L){return !L.ClaimedBy.IsEmpty();});if(Next->Loot.Num()>=128)return false;
 FAetherWorldLoot Loot;Loot.ClaimId=Instance;Loot.Location=Rule->Center+FVector(0,100,-85);Loot.Count=2;Next->Loot.Add(Loot);
 FAetherCampReceipt Receipt;Receipt.Definition=Definition;Receipt.Instance=Instance;Receipt.RespawnAfterUtc=FDateTime::UtcNow().ToUnixTimestamp()+FMath::CeilToInt(Rule->RespawnSeconds);
 Next->CampReceipts.RemoveAll([&](const auto& R){return R.Definition==Definition;});Next->CampReceipts.Add(Receipt);
 if(!WriteDatabase(Next))return false;SpawnLoot(Loot);return true;
}
FString AAetherFrontierMode::ClaimLoot(AAetherFrontierCharacter* C,FName Id)
{
 auto* PS=C?C->ProfileState():nullptr;auto* Actor=Prop(Id);if(!PS||!C->Alive()||!Actor||Actor->Service!="Loot"||FVector::DistSquared(C->GetActorLocation(),Actor->GetActorLocation())>FMath::Square(250.))return TEXT("Loot out of reach.");
 FCollisionQueryParams Q(SCENE_QUERY_STAT(Loot),false,C);Q.AddIgnoredActor(Actor);if(GetWorld()->LineTraceTestByChannel(C->GetActorLocation(),Actor->GetActorLocation(),ECC_Visibility,Q))return TEXT("Loot is obstructed.");
 auto* Next=DuplicateObject<UAetherFrontierSave>(Database,this);auto* Loot=Next->Loot.FindByPredicate([&](const auto& L){return Id==FName(*FString("Loot_"+L.ClaimId.ToString(EGuidFormats::Digits)));});
 if(!Loot||!Loot->ClaimedBy.IsEmpty())return TEXT("Already claimed.");auto Profile=PS->Profile;if(Profile.Revision==MAX_int32||!Profile.Add(Loot->Definition,Loot->Count))return TEXT("Inventory full; loot remains.");
 ++Profile.Revision;Loot->ClaimedBy=Profile.CharacterId;if(auto* Stored=Next->Profiles.FindByPredicate([&](const auto& P){return P.CharacterId==Profile.CharacterId;}))*Stored=Profile;else Next->Profiles.Add(Profile);
 if(!WriteDatabase(Next))return TEXT("Storage unavailable; loot unchanged.");PS->Profile=MoveTemp(Profile);PS->ForceNetUpdate();Props.Remove(Actor);Actor->Destroy();return TEXT("Shared loot claimed and saved once.");
}

void AAetherFrontierMode::CollectPublicFacts(FAetherWorldFacts& Facts) const
{
    if(!HasAuthority())return;
    // Backfill only a trusted persisted service outcome from older saves.
    if(Database->bSupplyRestored)Facts.Record("SupplyRestored","Pump");
    for(const auto& P:Props)if(IsValid(P)&&P->bInspectableFire&&!P->Reactive->bOwnerOnlyStimuli&&AetherGuide::CanInspectFire(P))
        Facts.Record(P->Service,P->Spec.Id);
}
void AAetherFrontierMode::RefreshWorldProgress()
{
    auto Facts=Database->WorldFacts;CollectPublicFacts(Facts);
    bool Changed=!Facts.Sources.OrderIndependentCompareEqual(Database->WorldFacts.Sources);
    TArray<TPair<AAetherPlayerState*,FAetherProfile>> Publish;
    for(TActorIterator<AAetherPlayerState> It(GetWorld());It;++It)if(!It->IsInactive()&&It->Profile.Revision<MAX_int32)
    {
        auto Next=It->Profile;if(!AetherQuests::Settle(Next,Facts,false))continue;
        ++Next.Revision;if(!Next.Validate())return;
        Publish.Emplace(*It,MoveTemp(Next));Changed=true;
    }
    if(!Changed)return;
    auto* Candidate=DuplicateObject<UAetherFrontierSave>(Database,this);Candidate->WorldFacts=MoveTemp(Facts);
    for(const auto& Pair:Publish)
    {
        const auto& Next=Pair.Value;
        if(auto* Stored=Candidate->Profiles.FindByPredicate([&](const auto& P){return P.CharacterId==Next.CharacterId;}))*Stored=Next;else Candidate->Profiles.Add(Next);
    }
    if(!CaptureWorldCandidate(Candidate)||!WriteDatabase(Candidate))return;
    for(auto& Pair:Publish){Pair.Key->Profile=MoveTemp(Pair.Value);Pair.Key->ForceNetUpdate();}
}
