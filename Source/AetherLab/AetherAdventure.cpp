#include "AetherAdventure.h"
#include "AetherCombat.h"
#include "AetherContent.h"
#include "ReactiveWorldSubsystem.h"
#include "Camera/PlayerCameraManager.h"
#include "Camera/CameraActor.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/Canvas.h"
#include "Engine/DirectionalLight.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/SkyLight.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Engine/TextureCube.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/PlatformMisc.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Net/UnrealNetwork.h"
#include "UnrealClient.h"
#include "UObject/ConstructorHelpers.h"

AAetherWorldObject::AAetherWorldObject()
{
    PrimaryActorTick.bCanEverTick = true; PrimaryActorTick.TickInterval = .1f;
    bReplicates = true; SetReplicateMovement(true); SetNetUpdateFrequency(10); SetNetCullDistanceSquared(FMath::Square(14000.f));
    Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh")); SetRootComponent(Mesh);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    Mesh->SetStaticMesh(Cube.Object); Mesh->SetCollisionProfileName(TEXT("BlockAllDynamic"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Surface(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    if (Surface.Succeeded()) Mesh->SetMaterial(0,Surface.Object);
    Reactive = CreateDefaultSubobject<UReactiveBodyComponent>(TEXT("Reactive")); Reactive->bEnableChaosOnBreak = false;
    Label = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Label")); Label->SetupAttachment(Mesh);
    Label->SetAbsolute(false,true,true); Label->SetHorizontalAlignment(EHTA_Center); Label->SetWorldSize(25); Label->SetTextRenderColor(FColor::White);
}
void AAetherWorldObject::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{ Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(AAetherWorldObject,Spec); DOREPLIFETIME(AAetherWorldObject,bEnabled); }
void AAetherWorldObject::ConfigureMaterial()
{
    Reactive->StableId = Spec.Id; Reactive->bParticipatesInSimulation = Spec.bInteractiveMaterial;
    Reactive->Preset = (Spec.Kind == EAetherObjectKind::Timber || Spec.Kind == EAetherObjectKind::Rope || Spec.Kind == EAetherObjectKind::Bridge) ? EReactiveMaterialPreset::Wood
        : Spec.Kind == EAetherObjectKind::Water || Spec.Kind == EAetherObjectKind::Cistern ? EReactiveMaterialPreset::Water
        : Spec.Kind == EAetherObjectKind::Oil ? EReactiveMaterialPreset::Oil : EReactiveMaterialPreset::Stone;
    Reactive->InteractionRadiusCm = Spec.Kind == EAetherObjectKind::Water ? 145 : Spec.Kind == EAetherObjectKind::Rope ? 25 : 60;
    Reactive->bIceControlsPawnCollision = Spec.Kind == EAetherObjectKind::Water;
    if (Spec.Kind == EAetherObjectKind::Rope)
    {auto* Asset=NewObject<UReactiveMaterialAsset>(this);Asset->Parameters.CutResistanceJ=20;Reactive->MaterialAsset=Asset;}
    if (Spec.Kind == EAetherObjectKind::Cistern)
    {
        auto* Asset = NewObject<UReactiveMaterialAsset>(this);
        Asset->Parameters.DryMassKg = .001; Asset->Parameters.SpecificHeatJPerKgK = 1000;
        Asset->Parameters.WaterCapacityKg = 8; Asset->Parameters.InitialFuelKg = 0; Asset->Parameters.Conductivity = .1; Asset->Parameters.bLiquidConductor = true;
        Reactive->MaterialAsset = Asset;
    }
}
void AAetherWorldObject::ApplySpec()
{
    if (!Spec.ArtMesh.IsNull()) if (auto* Art=Spec.ArtMesh.LoadSynchronous()) Mesh->SetStaticMesh(Art);
    Mesh->SetRelativeScale3D(Spec.Scale);
    Label->SetRelativeLocation(FVector(0,0,65)); Label->SetText(FText::FromString(Spec.Label));
    if (Spec.Kind == EAetherObjectKind::Water)
    { Reactive->bIceControlsPawnCollision = true; Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly); Mesh->SetCollisionResponseToChannel(ECC_Pawn,ECR_Ignore); }
    RefreshState();
}
void AAetherWorldObject::BeginPlay() { Super::BeginPlay(); ApplySpec(); }
void AAetherWorldObject::ReceiveEquipmentHit_Implementation(const FAetherEquipmentHit& Hit)
{
    if (!HasAuthority() || !bEnabled) return;
    FReactiveStimulus S; S.SourceActor=Hit.Source; S.ImpulseNs=Hit.ImpulseNs; S.CuttingWorkJ=Hit.CuttingWorkJ; Reactive->Inject(S);
}
void AAetherWorldObject::ResetFragments()
{ for (auto& F : Fragments) if (F.IsValid()) F->Destroy(); Fragments.Reset(); bFragmentsSpawned = false; }
void AAetherWorldObject::RefreshState()
{
    const auto& S = Reactive->State;
    const bool Destroyed = S.bBroken && (Spec.Kind == EAetherObjectKind::Timber || Spec.Kind == EAetherObjectKind::Rope || Spec.Kind == EAetherObjectKind::Sigil);
    Mesh->SetVisibility(bEnabled && !Destroyed);
    if (!bEnabled || Destroyed) Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    else if (Spec.Kind != EAetherObjectKind::Water) Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    else Reactive->RefreshPresentation();
    auto* Mat = Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0)); if (!Mat) Mat = Mesh->CreateAndSetMaterialInstanceDynamic(0);
    if (Mat) Mat->SetVectorParameterValue(TEXT("Color"), S.bBurning ? FLinearColor(1,.09f,.005f) : S.IceFraction >= .95 ? FLinearColor(.32f,.85f,1) : Spec.Color);
    if (!Spec.ArtMesh.IsNull()) for (int32 I=0; I<Mesh->GetNumMaterials(); ++I)
    {
        auto* Surface=Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(I)); if (!Surface) Surface=Mesh->CreateAndSetMaterialInstanceDynamic(I);
        if (Surface) { Surface->SetScalarParameterValue(TEXT("Wetness"),FMath::Clamp(S.WaterKg,0.0,1.0)); Surface->SetScalarParameterValue(TEXT("FrostAmount"),S.IceFraction); Surface->SetScalarParameterValue(TEXT("BurnAmount"),S.bBurning?.65f:0.f); }
    }
    if (Destroyed && !bFragmentsSpawned && HasAuthority())
    {
        bFragmentsSpawned = true;
        // Authored three-piece rigid-body breakup. Replaced by cooked Geometry Collections in art production.
        for (int32 I = 0; I < 3; ++I)
            if (auto* Piece = GetWorld()->SpawnActor<AStaticMeshActor>(GetActorLocation() + FVector(0,0,(I - 1) * 30),GetActorRotation()))
            {
                Piece->SetMobility(EComponentMobility::Movable); Piece->SetReplicates(true); Piece->SetReplicateMovement(true);
                auto* P = Piece->GetStaticMeshComponent(); P->SetIsReplicated(true); P->SetStaticMesh(Mesh->GetStaticMesh());
                Piece->SetActorScale3D(Spec.Scale * FVector(1,1,.3)); P->SetCollisionProfileName(TEXT("PhysicsActor")); P->SetCollisionResponseToChannel(ECC_Pawn,ECR_Ignore);
                P->SetSimulatePhysics(true); P->AddImpulse(FVector(100 * (I - 1),80,100)); Piece->SetLifeSpan(12); Fragments.Add(Piece);
            }
    }
}
void AAetherWorldObject::Tick(float Dt)
{
    Super::Tick(Dt); RefreshState();
    if (auto* PC = GetWorld()->GetFirstPlayerController(); PC && PC->PlayerCameraManager)
    { Label->SetWorldRotation((PC->PlayerCameraManager->GetCameraLocation() - Label->GetComponentLocation()).Rotation());
      Label->SetVisibility(!Spec.Label.IsEmpty() && FVector::DistSquared(PC->PlayerCameraManager->GetCameraLocation(),GetActorLocation()) < FMath::Square(1700.0)); }
}
void AAetherAdventureState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{ Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(AAetherAdventureState,Quest); DOREPLIFETIME(AAetherAdventureState,bRain); }
void AAetherAdventureState::BeginPlay()
{
    Super::BeginPlay();
    // Lighting is presentation-only and is created on each rendering peer.
    if (GetNetMode()==NM_DedicatedServer) return;
    for(TActorIterator<ADirectionalLight> It(GetWorld());It;++It)if(It->ActorHasTag("AetherBakedLighting"))return;
    if (auto* L=GetWorld()->SpawnActor<ADirectionalLight>(FVector(0,0,1200),FRotator(-48,-30,0)))
    { L->GetLightComponent()->SetMobility(EComponentMobility::Movable); L->GetLightComponent()->SetIntensity(3);
      Cast<UDirectionalLightComponent>(L->GetLightComponent())->SetAtmosphereSunLight(true); Cast<UDirectionalLightComponent>(L->GetLightComponent())->SetForwardShadingPriority(1); }
    if (auto* L=GetWorld()->SpawnActor<ADirectionalLight>(FVector(0,0,1400),FRotator(-28,160,0)))
    { L->GetLightComponent()->SetMobility(EComponentMobility::Movable); L->GetLightComponent()->SetIntensity(1.5f); L->GetLightComponent()->SetCastShadows(false); }
    GetWorld()->SpawnActor<ASkyAtmosphere>();
    if (auto* Sky=GetWorld()->SpawnActor<ASkyLight>())
    {
        auto* Light=Sky->GetLightComponent(); Light->SetMobility(EComponentMobility::Movable); Light->SourceType=SLS_SpecifiedCubemap;
        Light->bLowerHemisphereIsBlack=false; Light->SetIntensity(1.3f);
        Light->SetCubemap(LoadObject<UTextureCube>(nullptr,TEXT("/Engine/MapTemplates/Sky/DaylightAmbientCubemap.DaylightAmbientCubemap")));
        Light->RecaptureSky();
    }
}
AAetherAdventureMode::AAetherAdventureMode()
{ PrimaryActorTick.bCanEverTick = true; DefaultPawnClass = AAetherCharacter::StaticClass(); HUDClass = AAetherAdventureHUD::StaticClass(); GameStateClass = AAetherAdventureState::StaticClass(); }
void AAetherModularAdventureMode::InitGame(const FString& MapName,const FString& Options,FString& ErrorMessage)
{
    if (auto* Content=UAetherGameContent::Load()) LevelDefinition=Content->Abbey;
    if (!LevelDefinition) ErrorMessage=TEXT("Missing SwordMagic content. Run the asset import first.");
    Super::InitGame(MapName,Options,ErrorMessage);
}
void AAetherAdventureMode::RestartPlayer(AController* C)
{
    if (!C) return;
    if (!LevelDefinition && FParse::Param(FCommandLine::Get(),TEXT("AetherArtLevel")))
        if (auto* Content=UAetherGameContent::Load()) LevelDefinition=Content->Abbey;
    RestartPlayerAtTransform(C,LevelDefinition?LevelDefinition->PlayerSpawn:FTransform(FRotator::ZeroRotator,FVector(-850,0,110)));
    if (C->GetPawn()) C->SetControlRotation(FRotator(-8,0,0));
}
AAetherWorldObject* AAetherAdventureMode::Object(FName Id, EAetherObjectKind Kind, FVector P, FVector Scale, const FString& Label, double Water)
{
    auto* A = GetWorld()->SpawnActorDeferred<AAetherWorldObject>(AAetherWorldObject::StaticClass(),FTransform(P),nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    A->Spec.Id = Id; A->Spec.Kind = Kind; A->Spec.Scale = Scale; A->Spec.Label = Label;
    A->Spec.bInteractiveMaterial = Kind == EAetherObjectKind::Timber || Kind == EAetherObjectKind::Rope || Kind == EAetherObjectKind::Water
        || Kind == EAetherObjectKind::Oil || Kind == EAetherObjectKind::Cistern || Kind == EAetherObjectKind::Sigil;
    if (Kind == EAetherObjectKind::Timber || Kind == EAetherObjectKind::Rope || Kind == EAetherObjectKind::Bridge) A->Spec.Color = FLinearColor(.3f,.14f,.045f);
    if (Kind == EAetherObjectKind::Water || Kind == EAetherObjectKind::Cistern) A->Spec.Color = FLinearColor(.02f,.18f,.36f);
    if (Kind == EAetherObjectKind::Oil) A->Spec.Color = FLinearColor(.1f,.055f,.02f);
    if (Kind == EAetherObjectKind::Apprentice || Kind == EAetherObjectKind::Record || Kind == EAetherObjectKind::Witness || Kind == EAetherObjectKind::Sigil || Kind == EAetherObjectKind::Return) A->Spec.Color = FLinearColor(.8f,.52f,.08f);
    A->ConfigureMaterial(); A->Reactive->InitialWaterKg = Water;
    if (Kind == EAetherObjectKind::Water) { A->Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly); A->Mesh->SetCollisionResponseToChannel(ECC_Pawn,ECR_Ignore); }
    if (Kind == EAetherObjectKind::Bridge) A->bEnabled = false;
    UGameplayStatics::FinishSpawningActor(A,FTransform(P)); Objects.Add(A); return A;
}
AAetherWorldObject* AAetherAdventureMode::FindObject(FName Id) const
{ for (const auto& O : Objects) if (O && O->Spec.Id == Id) return O; return nullptr; }
void AAetherAdventureMode::BuildAbbey()
{
    if (LevelDefinition) { BuildArtAbbey(); return; }
    Object(TEXT("EntranceFloor"),EAetherObjectKind::Stone,{-650,0,-30},{19,22,.6});
    Object(TEXT("CourtFloor"),EAetherObjectKind::Stone,{2350,0,-30},{29,22,.6});
    Object(TEXT("BelfryFloor"),EAetherObjectKind::Stone,{5000,0,-30},{24,22,.6});
    Object(TEXT("ChannelBed"),EAetherObjectKind::Stone,{600,0,-155},{6,22,.3});
    Object(TEXT("MaintenancePath"),EAetherObjectKind::Stone,{600,780,-10},{6,2.6,.2},TEXT("MAINTENANCE PATH / NO MAGIC NEEDED"));
    Object(TEXT("Rope"),EAetherObjectKind::Rope,{160,-220,110},{.22,.22,2.2},TEXT("BRIDGE ROPE / HEAVY STRIKE OR FIRE"));
    for (int32 I=0; I<3; ++I)
    {
        Object(*FString::Printf(TEXT("Bridge%d"),I),EAetherObjectKind::Bridge,{400.f+I*200, -350, 5},{2,3,.2});
        Object(*FString::Printf(TEXT("IcePath%d"),I),EAetherObjectKind::Water,{400.f+I*200, 100, -25},{2,3,.15},I==0 ? TEXT("SHALLOW CHANNEL / FROST MAKES A PATH") : TEXT(""),.5);
    }
    Object(TEXT("EntryMarker"),EAetherObjectKind::Return,{-1050,-360,80},{.6,.6,1.6},TEXT("GREYFORD / E TO RETURN AFTER THE QUEST"));
    Object(TEXT("WaterWell"),EAetherObjectKind::Cistern,{-500,400,60},{1,1,1.2},TEXT("CISTERN / E TRANSFERS FINITE WATER"),8);
    Object(TEXT("CourtWater0"),EAetherObjectKind::Water,{2050,-100,8},{3,3,.15},TEXT("WET GROUND / LIGHTNING ALSO HURTS YOU"),.7);
    Object(TEXT("CourtWater1"),EAetherObjectKind::Water,{2330,-100,8},{3,3,.15},TEXT(""),.4);
    Object(TEXT("OilBowl"),EAetherObjectKind::Oil,{2370,280,60},{.8,.8,1.2},TEXT("LAMP OIL / BURNS IN THE OPEN"));
    Object(TEXT("CourtBeam"),EAetherObjectKind::Timber,{2550,330,120},{.4,.4,2.4},TEXT("WEAK TIMBER / BURNS AND BREAKS"));
    Object(TEXT("Apprentice"),EAetherObjectKind::Apprentice,{3220,-650,70},{.5,.5,1.4},TEXT("LAMP APPRENTICE / E RESCUE AND HEAR TESTIMONY"));
    Object(TEXT("Record"),EAetherObjectKind::Record,{3330,650,65},{.8,.6,1.3},TEXT("OATH RECORD / E READ"));
    Object(TEXT("Witness"),EAetherObjectKind::Witness,{4490,-760,80},{.5,.5,1.6},TEXT("WITNESS BELL / E RELEASE THE OATH"));
    Object(TEXT("Sigil"),EAetherObjectKind::Sigil,{5740,0,100},{.7,.7,2},TEXT("ANCIENT SIGIL / E KEEP HERE / Q TAKE HOME"));
    for (int32 I=0; I<4; ++I)
        Object(*FString::Printf(TEXT("ArenaWater%d"),I),EAetherObjectKind::Water,{4600.f+I*260,200,8},{2.6,3,.15},I==0 ? TEXT("FLOOD CHANNEL") : TEXT(""),I==0 ?.8:0);
    Object(TEXT("ArenaCistern"),EAetherObjectKind::Cistern,{4400,440,55},{1,1,1.1},TEXT("SLUICE / E RELEASE WATER"),6);
    // Stone walls, buttresses and an open belfry keep routes legible in the graybox.
    for (int32 I=0; I<14; ++I)
    {
        for (int32 Side : {-1,1})
            Object(*FString::Printf(TEXT("Pillar%d_%d"),I,Side),EAetherObjectKind::Stone,{-1000.f+I*540,Side*1050.f,200},{.9,.9,4});
    }
    Object(TEXT("NorthWall"),EAetherObjectKind::Stone,{2450,1140,100},{76,.35,2});
    Object(TEXT("SouthWall"),EAetherObjectKind::Stone,{2450,-1140,100},{76,.35,2});
    Object(TEXT("EastWall"),EAetherObjectKind::Stone,{6250,0,160},{.4,23,3.2});
    Object(TEXT("WestWall"),EAetherObjectKind::Stone,{-1630,0,160},{.4,23,3.2});
    Object(TEXT("ArchLeft"),EAetherObjectKind::Stone,{3800,-720,180},{1.5,7.2,3.6});
    Object(TEXT("ArchRight"),EAetherObjectKind::Stone,{3800,720,180},{1.5,7.2,3.6});
    Object(TEXT("ArchLintel"),EAetherObjectKind::Stone,{3800,0,420},{1.5,22,1.2},TEXT("THE BROKEN BELL ABBEY"));
    Object(TEXT("BelfryLintel"),EAetherObjectKind::Stone,{5700,0,580},{1.2,20,1.4});
    for (int32 I=0; I<3; ++I)
    {
        const FVector P = I==0 ? FVector(2070,-90,100) : I==1 ? FVector(2640,500,100) : FVector(5090,-100,100);
        auto* C=GetWorld()->SpawnActorDeferred<AAetherCharacter>(AAetherCharacter::StaticClass(),FTransform(FRotator(0,180,0),P),nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        C->Fighter = I==0 ? EAetherFighter::ShieldGuard : I==1 ? EAetherFighter::FireCaster : EAetherFighter::BellKnight;
        C->Reactive->StableId = I==0 ? TEXT("Guard") : I==1 ? TEXT("Caster") : TEXT("Olen");
        UGameplayStatics::FinishSpawningActor(C,FTransform(FRotator(0,180,0),P)); Enemies.Add(C); if (I==2) Boss=C;
    }
}
void AAetherAdventureMode::BuildArtAbbey()
{
    TSet<FName> Ids;
    for (const auto& Entry:LevelDefinition->Objects)
    {
        if (Entry.Spec.Id.IsNone() || Ids.Contains(Entry.Spec.Id) || Entry.Transform.ContainsNaN())
        { UE_LOG(LogTemp,Error,TEXT("Invalid/duplicate level object identity")); return; }
        Ids.Add(Entry.Spec.Id);
    }
    const TArray<FName> RequiredIds={TEXT("Rope"),TEXT("Bridge0"),TEXT("Bridge1"),TEXT("Bridge2"),TEXT("IcePath0"),TEXT("CourtWater0"),TEXT("CourtWater1"),TEXT("ArenaWater0"),TEXT("ArenaWater1"),TEXT("ArenaWater2"),TEXT("ArenaWater3"),TEXT("ArenaCistern"),TEXT("WaterWell"),TEXT("Apprentice"),TEXT("Record"),TEXT("Witness"),TEXT("Sigil"),TEXT("EntryMarker")};
    for (FName Required:RequiredIds)
        if (!Ids.Contains(Required)) { UE_LOG(LogTemp,Error,TEXT("Scenario missing required binding: %s"),*Required.ToString()); return; }
    if (auto* Mesh=LevelDefinition->StaticShell.LoadSynchronous())
    {
        auto* Shell=GetWorld()->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(),LevelDefinition->ShellTransform);
        Shell->SetMobility(EComponentMobility::Movable); Shell->SetReplicates(true); Shell->SetReplicateMovement(true);
        Shell->GetStaticMeshComponent()->SetIsReplicated(true); Shell->GetStaticMeshComponent()->SetStaticMesh(Mesh);
        Shell->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
    }
    for (const auto& Entry:LevelDefinition->Objects)
    {
        auto* A=GetWorld()->SpawnActorDeferred<AAetherWorldObject>(AAetherWorldObject::StaticClass(),Entry.Transform,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        A->Spec=Entry.Spec; A->bEnabled=Entry.bInitiallyEnabled; A->ConfigureMaterial(); A->Reactive->InitialWaterKg=Entry.InitialWaterKg;
        UGameplayStatics::FinishSpawningActor(A,Entry.Transform); Objects.Add(A);
    }
    for (const auto& Entry:LevelDefinition->Enemies)
    {
        auto* C=GetWorld()->SpawnActorDeferred<AAetherCharacter>(AAetherCharacter::StaticClass(),Entry.Transform,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        C->Fighter=EAetherFighter(Entry.Archetype); C->CharacterDefinition=Entry.Definition; C->Reactive->StableId=Entry.Id;
        UGameplayStatics::FinishSpawningActor(C,Entry.Transform); Enemies.Add(C); if (C->Fighter==EAetherFighter::BellKnight) Boss=C;
    }
}
void AAetherAdventureMode::BeginPlay()
{
    Super::BeginPlay(); bSmoke=FParse::Param(FCommandLine::Get(),TEXT("AetherAdventureSmoke"));
    bEquipmentSmoke=FParse::Param(FCommandLine::Get(),TEXT("AetherEquipmentSmoke")); bSmoke|=bEquipmentSmoke;
    bCapture=FParse::Param(FCommandLine::Get(),TEXT("AetherAdventureCapture"));
    if (FParse::Param(FCommandLine::Get(),TEXT("AetherArtLevel"))) if (auto* Content=UAetherGameContent::Load()) LevelDefinition=Content->Abbey;
    if (bSmoke) SaveSlot=bEquipmentSmoke?TEXT("AetherSmoke_Equipment_v2"):TEXT("AetherSmoke_BCDE_v2");
    BuildAbbey(); UE_LOG(LogTemp,Display,TEXT("AETHER_ADVENTURE_READY objects=%d enemies=%d"),Objects.Num(),Enemies.Num());
}
void AAetherAdventureMode::UpdateWorld(float Dt)
{
    auto* GS=GetGameState<AAetherAdventureState>(); if (!GS || Objects.IsEmpty()) return;
    for (int32 I=0; I<3; ++I) FindObject(*FString::Printf(TEXT("Bridge%d"),I))->bEnabled=FindObject(TEXT("Rope"))->Reactive->State.bBroken;
    for (TActorIterator<AAetherCharacter> It(GetWorld()); It; ++It)
        if (It->Fighter==EAetherFighter::Player && It->Alive())
        {
            if (It->GetActorLocation().X>(LevelDefinition?LevelDefinition->CourtEntryX:1100)) GS->Quest.Phase=FMath::Max<uint8>(GS->Quest.Phase,1);
            if (It->GetActorLocation().X>(LevelDefinition?LevelDefinition->ArenaEntryX:3900)) GS->Quest.Phase=FMath::Max<uint8>(GS->Quest.Phase,2);
            if (It->GetActorLocation().Z < -350) It->SetActorLocation(LevelDefinition?LevelDefinition->PlayerSpawn.GetLocation():FVector(-850,0,110),false,nullptr,ETeleportType::TeleportPhysics);
        }
    if (Boss && !Boss->Alive()) { GS->Quest.Phase=FMath::Max<uint8>(GS->Quest.Phase,3); GS->Quest.bGuardianPeace=Boss->bPacified; }
    WaterTimer += Dt;
    if (WaterTimer >= .2f)
    {
        WaterTimer=0; auto* Sim=GetWorld()->GetSubsystem<UReactiveWorldSubsystem>();
        auto Flow=[Sim,this](FName A,FName B)
        {
            auto* From=FindObject(A)->Reactive.Get(); auto* To=FindObject(B)->Reactive.Get();
            const double Difference=From->State.WaterKg-To->State.WaterKg;
            if (Difference>0) Sim->TransferWater(From,To,FMath::Min(.1,Difference*.25));
            else Sim->TransferWater(To,From,FMath::Min(.1,-Difference*.25));
        };
        Flow(TEXT("CourtWater0"),TEXT("CourtWater1"));
        for (int32 I=0; I<3; ++I) Flow(*FString::Printf(TEXT("ArenaWater%d"),I),*FString::Printf(TEXT("ArenaWater%d"),I+1));
    }
}
FString AAetherAdventureMode::Interact(AAetherCharacter* P, bool Alternate)
{
    if (!P || !P->Alive()) return TEXT("You cannot interact while downed. F9 loads the last save.");
    AAetherWorldObject* Target=nullptr; double Best=FMath::Square(230.0);
    for (const auto& O:Objects)
    {
        if (!O || uint8(O->Spec.Kind)<uint8(EAetherObjectKind::Cistern)) continue;
        const double D=FVector::DistSquared(P->GetActorLocation(),O->GetActorLocation());
        FCollisionQueryParams Params(SCENE_QUERY_STAT(AetherInteract),false,P); Params.AddIgnoredActor(O);
        if (D<Best && !GetWorld()->LineTraceTestByChannel(P->GetActorLocation(),O->GetActorLocation(),ECC_Visibility,Params)) { Target=O; Best=D; }
    }
    if (!Target) return TEXT("Move within 2.3 m of a marked object, with a clear line of sight.");
    auto* GS=GetGameState<AAetherAdventureState>(); auto& Q=GS->Quest;
    switch (Target->Spec.Kind)
    {
    case EAetherObjectKind::Cistern:
    {
        auto* Sim=GetWorld()->GetSubsystem<UReactiveWorldSubsystem>();
        if (Target->Spec.Id==TEXT("ArenaCistern"))
        { const double Kg=Sim->TransferWater(Target->Reactive,FindObject(TEXT("ArenaWater0"))->Reactive,.8); return FString::Printf(TEXT("Sluice transferred %.2f kg into the arena."),Kg); }
        // Fill a carried reservoir through the same core transfer, then withdraw it from a temporary body.
        const double Available=Target->Reactive->State.WaterKg*(1-Target->Reactive->State.IceFraction);
        const double Amount=FMath::Min3(.5,double(3-P->WaterReserveKg),Available);
        if (Amount<=.001) return TEXT("The cistern is frozen/empty, or your waterskin is full.");
        const double Taken=Sim->WithdrawWater(Target->Reactive,Amount);
        P->WaterReserveKg+=float(Taken); return FString::Printf(TEXT("Waterskin +%.2f kg. This cistern is finite."),Taken);
    }
    case EAetherObjectKind::Apprentice: Q.bApprentice=true; Q.bTestimony=true; return TEXT("Apprentice rescued: 'Olen swore to protect the people, not the crown.'");
    case EAetherObjectKind::Record: Q.bRecord=true; return TEXT("Record recovered: the guardian's vow can be released by two witnesses.");
    case EAetherObjectKind::Witness:
        if (!Q.bRecord || !Q.bTestimony) return TEXT("Bring the oath record and the apprentice's testimony.");
        if (!Boss || !Boss->Alive()) return TEXT("The guardian has already been resolved.");
        if (Boss->bWindingUp) return TEXT("The bell is drowned by the knight's attack. Wait for recovery.");
        Boss->Pacify(); Q.bGuardianPeace=true; Q.Phase=3; return TEXT("Olen's oath is released. The road is open without killing him.");
    case EAetherObjectKind::Sigil:
        if (Q.Phase<3) return TEXT("Resolve the bell knight first.");
        if (Q.Resolution) return TEXT("This choice has already been recorded.");
        if (Target->Reactive->State.bBroken) { Q.Resolution=3; Q.Phase=4; return TEXT("The sigil is broken. Greyford will rebuild a manual crossing."); }
        if (!Alternate && !Q.bApprentice) return TEXT("The apprentice must maintain the sigil here. Q carries it to Greyford instead.");
        Q.Resolution=Alternate?2:1; Q.Phase=4;
        return Alternate?TEXT("Sigil taken: Greyford receives light; the abbey loses its protection."):TEXT("Sigil left: the apprentice maintains the crossing and the abbey.");
    case EAetherObjectKind::Return:
        if (Q.Phase<4) return TEXT("Reach the abbey, resolve Olen and decide the sigil's fate.");
        if (!Q.bRewardGranted) { Q.bRewardGranted=true; Q.Crowns+=80+(Q.bApprentice?20:0); }
        Q.Phase=5; return TEXT("Quest complete. Your choice and reward are recorded. F5 saves.");
    default: return TEXT("Nothing to use here.");
    }
}
FString AAetherAdventureMode::SaveAdventure(AAetherCharacter* P)
{
    if (GetNetMode()!=NM_Standalone) return TEXT("This prototype saves standalone games only.");
    if (!P || !P->Alive() || !P->Ready()) return TEXT("Save while alive and outside an action.");
    for (const auto& C:Enemies) if (C->Alive() && FVector::DistSquared(C->GetActorLocation(),P->GetActorLocation())<FMath::Square(1100.0)) return TEXT("Move out of combat before saving.");
    for (TActorIterator<AAetherProjectile> It(GetWorld());It;++It) return TEXT("Wait for the active projectile before saving.");
    auto* Save=NewObject<UAetherAdventureSave>(); Save->Quest=GetGameState<AAetherAdventureState>()->Quest;
    Save->Layout=LevelDefinition?LevelDefinition->LayoutId:FName(TEXT("BrokenBell_01"));
    auto* WorldSim = GetWorld()->GetSubsystem<UReactiveWorldSubsystem>();
    if (!WorldSim->Capture(Save->World)) return TEXT("World has pending reactions or duplicate IDs. Retry after the next simulation step.");
    const auto& Weather = WorldSim->GetSimulation()->GetEnvironment();
    Save->AmbientTemperatureC=Weather.TemperatureC; Save->RainKgPerM2Sec=Weather.RainKgPerM2Sec; Save->WindMPerSec=Weather.WindMPerSec;
    for (TActorIterator<AAetherCharacter> It(GetWorld());It;++It)
    { FAetherCharacterSave C; C.Id=It->Reactive->StableId; C.Health=It->Health(); C.Mana=It->Mana(); C.Stamina=It->Stamina(); C.Water=It->WaterReserveKg; C.bPacified=It->bPacified; C.Equipment=It->Equipment->Slots; Save->Characters.Add(C); }
    return UGameplayStatics::SaveGameToSlot(Save,SaveSlot,0)?TEXT("Saved: materials, characters, quest and rewards."):TEXT("Save write failed.");
}
FString AAetherAdventureMode::LoadAdventure(AAetherCharacter* P)
{
    if (GetNetMode()!=NM_Standalone) return TEXT("This prototype loads standalone games only.");
    auto* Save=Cast<UAetherAdventureSave>(UGameplayStatics::LoadGameFromSlot(SaveSlot,0));
    if (!Save || Save->Version!=2 || Save->Layout!=(LevelDefinition?LevelDefinition->LayoutId:FName(TEXT("BrokenBell_01"))) || Save->Quest.Phase>5 || Save->Quest.Resolution>3) return TEXT("No compatible save was found.");
    Reactive::FSimulation Validation; Reactive::FEnvironment Weather;
    Weather.TemperatureC=Save->AmbientTemperatureC; Weather.RainKgPerM2Sec=Save->RainKgPerM2Sec; Weather.WindMPerSec=Save->WindMPerSec;
    if (!Validation.SetEnvironment(Weather)) return TEXT("Save weather data is invalid.");
    TMap<FName,AAetherCharacter*> Characters;
    for (TActorIterator<AAetherCharacter> It(GetWorld());It;++It) Characters.Add(It->Reactive->StableId,*It);
    if (Characters.Num()!=Save->Characters.Num()) return TEXT("Save character roster differs from this level.");
    TSet<FName> Seen;
    for (const auto& C:Save->Characters)
    {
        if (!Characters.Contains(C.Id) || Seen.Contains(C.Id) || !FMath::IsFinite(C.Health) || C.Health<0 || C.Health>320
            || !FMath::IsFinite(C.Mana) || C.Mana<0 || C.Mana>100 || !FMath::IsFinite(C.Stamina) || C.Stamina<0 || C.Stamina>100
            || !FMath::IsFinite(C.Water) || C.Water<0 || C.Water>3) return TEXT("Save character data is invalid.");
        if (!Characters[C.Id]->Equipment->ValidateLoadout(C.Equipment)) return TEXT("Save equipment is invalid. World unchanged.");
        Seen.Add(C.Id);
    }
    if (!GetWorld()->GetSubsystem<UReactiveWorldSubsystem>()->Restore(Save->World)) return TEXT("Save material data/layout is invalid. World unchanged.");
    GetWorld()->GetSubsystem<UReactiveWorldSubsystem>()->SetWeather(Weather.TemperatureC,Weather.RainKgPerM2Sec,Weather.WindMPerSec);
    for (TActorIterator<AAetherProjectile> It(GetWorld());It;++It) It->Destroy();
    for (const auto& C:Save->Characters) { auto* Actor=Characters[C.Id]; Actor->ResetCombat(); Actor->bPacified=C.bPacified; Actor->SetVitals(C.Health,C.Mana,C.Stamina); Actor->WaterReserveKg=C.Water; Actor->Equipment->RestoreLoadout(C.Equipment); }
    GetGameState<AAetherAdventureState>()->Quest=Save->Quest;
    for (const auto& O:Objects) O->ResetFragments(); UpdateWorld(0);
    return TEXT("Loaded: the saved world and quest have been restored.");
}
void AAetherAdventureMode::Check(bool Pass,const TCHAR* Name)
{ if (!Pass) ++SmokeFailures; UE_LOG(LogTemp,Display,TEXT("BCDE_CHECK %s %s"),Pass?TEXT("PASS"):TEXT("FAIL"),Name); }
void AAetherAdventureMode::SmokeStep()
{
    auto* P=Cast<AAetherCharacter>(UGameplayStatics::GetPlayerPawn(this,0)); if (!P || Elapsed<1 || Elapsed<SmokeWait) return;
    auto* GS=GetGameState<AAetherAdventureState>();
    auto Aim=[P](FVector Target) { P->GetController()->SetControlRotation((Target-P->GetActorLocation()-FVector(0,0,55)).Rotation()); };
    auto Near=[P](AAetherWorldObject* O) { P->SetActorLocation(O->GetActorLocation()+FVector(-140,0,100),false,nullptr,ETeleportType::TeleportPhysics); };
    switch (SmokeStage++)
    {
    case 0:
        P->SetVitals(100,0,100); Check(!P->TrySpell(0),TEXT("GAS rejects insufficient mana"));
        P->SetVitals(100,100,100); P->SetActorLocation(LevelDefinition?FindObject(TEXT("IcePath0"))->GetActorLocation()+FVector(-220,0,140):FVector(400,-120,110)); Aim(FindObject(TEXT("IcePath0"))->GetActorLocation());
        Check(P->TrySpell(2),TEXT("GAS accepts frost spec")); Check(P->Mana()<=80.01f,TEXT("GAS commits mana once")); SmokeWait=Elapsed+1; break;
    case 1:
        Check(FindObject(TEXT("IcePath0"))->Reactive->State.IceFraction>.95,TEXT("Frost freezes water with latent heat"));
        Check(FindObject(TEXT("IcePath0"))->Mesh->GetCollisionResponseToChannel(ECC_Pawn)==ECR_Block,TEXT("Ice supports pawn collision"));
        P->ResetCombat(); P->SetActorLocation(FVector(-850,0,110));
        Check(SaveAdventure(P).StartsWith(TEXT("Saved:")),TEXT("Versioned save captures world"));
        P->SetActorLocation(FindObject(TEXT("Rope"))->GetActorLocation()+FVector(-125,0,0)); P->SetActorRotation(FRotator::ZeroRotator); P->GetController()->SetControlRotation(FRotator::ZeroRotator); P->ServerAttack(true); SmokeWait=Elapsed+.4f; break;
    case 2:
        Check(FindObject(TEXT("Rope"))->Reactive->State.bBroken,TEXT("Sword heavy strike breaks rope"));
        Check(FindObject(TEXT("Bridge0"))->bEnabled,TEXT("Broken support opens alternate bridge"));
        P->ResetCombat(); P->SetVitals(100,100,100); P->SetActorLocation(LevelDefinition?Enemies[0]->GetActorLocation()+FVector(-400,0,0):FVector(1590,-90,110)); Aim(Enemies[0]->GetActorLocation());
        Check(P->TrySpell(3),TEXT("GAS accepts targeted lightning")); SmokeWait=Elapsed+.3f; break;
    case 3:
    {
        Check(Enemies[0]->DamageReceivedCount>0,TEXT("Conduction reaches character damage"));
        Check(Enemies[0]->LastDamager.Get()==P,TEXT("Electrical damage retains source actor"));
        Check(!GS->Quest.bApprentice,TEXT("Quest starts without rescue")); Interact(P,false); Check(!GS->Quest.bApprentice,TEXT("Remote interaction cannot rescue"));
        Near(FindObject(TEXT("Apprentice"))); Interact(P,false); Check(GS->Quest.bApprentice&&GS->Quest.bTestimony,TEXT("Rescue grants testimony"));
        Near(FindObject(TEXT("Record"))); Interact(P,false); Check(GS->Quest.bRecord,TEXT("Record interaction grants evidence"));
        Near(FindObject(TEXT("Witness"))); Interact(P,false); Check(Boss->bPacified,TEXT("Witness route resolves guardian"));
        Near(FindObject(TEXT("Sigil"))); Interact(P,false); Check(GS->Quest.Resolution==1,TEXT("Rescued apprentice enables local sigil choice"));
        Near(FindObject(TEXT("EntryMarker"))); Interact(P,false); const int32 Reward=GS->Quest.Crowns; Interact(P,false);
        Check(GS->Quest.Phase==5&&Reward==100&&GS->Quest.Crowns==Reward,TEXT("Return reward is idempotent"));
        Check(LoadAdventure(P).StartsWith(TEXT("Loaded:")),TEXT("Load restores full checkpoint"));
        Check(!FindObject(TEXT("Rope"))->Reactive->State.bBroken&&!GS->Quest.bApprentice&&Boss->Alive(),TEXT("Load restores structure quest and guardian"));
        Check(FindObject(TEXT("IcePath0"))->Reactive->State.IceFraction>.95,TEXT("Ice persists across save/load"));
        SmokeWait=Elapsed+.3f; break;
    }
    case 4:
        P->ResetCombat(); P->SetVitals(100,100,100); Boss->SetVitals(32,100,100);
        P->SetActorLocation(Boss->GetActorLocation()+FVector(-140,0,0)); P->SetActorRotation(FRotator::ZeroRotator); P->GetController()->SetControlRotation(FRotator::ZeroRotator);
        P->ServerAttack(true); SmokeWait=Elapsed+.5f; break;
    case 5:
        Check(!Boss->Alive()&&!Boss->bPacified,TEXT("Sword route defeats guardian without spell gates"));
        Near(FindObject(TEXT("Sigil"))); Interact(P,true); Check(GS->Quest.Resolution==2,TEXT("Carry sigil branch works without apprentice"));
        Near(FindObject(TEXT("EntryMarker"))); Interact(P,false); Check(GS->Quest.Crowns==80,TEXT("Alternate ending has its own reward"));
        P->WaterReserveKg=2.5f; Near(FindObject(TEXT("WaterWell"))); Interact(P,false);
        Check(FMath::IsNearlyEqual(P->WaterReserveKg,3.f),TEXT("Cistern refills finite player inventory"));
        Check(GetWorld()->GetSubsystem<UReactiveWorldSubsystem>()->GetSimulation()->Find(FindObject(TEXT("WaterWell"))->Reactive->GetBodyId())->WaterKg<8,TEXT("Refill withdraws world water"));
        SmokeWait=Elapsed+.2f; break;
    default:
        UE_LOG(LogTemp,Display,TEXT("AETHER_BCDE_SMOKE_%s failures=%d"),SmokeFailures?TEXT("FAIL"):TEXT("PASS"),SmokeFailures);
        FPlatformMisc::RequestExitWithStatus(false,SmokeFailures?1:0); break;
    }
}
void AAetherAdventureMode::Tick(float Dt)
{
    Super::Tick(Dt); Elapsed+=Dt; UpdateWorld(Dt);
    if (bEquipmentSmoke) EquipmentSmokeStep(); else if (bSmoke) SmokeStep();
    if (FParse::Param(FCommandLine::Get(),TEXT("AetherNetServer")))
    {
        if (!bNetSetup && GetNumPlayers()>0)
        { bNetSetup=true; FReactiveStimulus S; S.HeatJ=-250000; FindObject(TEXT("IcePath0"))->Reactive->Inject(S);
          for (const auto& Enemy:Enemies) if (Enemy->Fighter==EAetherFighter::ShieldGuard) Enemy->Equipment->Equip(TEXT("TrainingHammer"));
          GetGameState<AAetherAdventureState>()->Quest.bRecord=true; UE_LOG(LogTemp,Display,TEXT("AETHER_NET_BASELINE_READY")); }
        if (Elapsed>120) FPlatformMisc::RequestExitWithStatus(false,0);
    }
    if (bCapture && LevelDefinition)
    {
        if (Elapsed>8+CaptureStage*2)
        {
            auto* P=Cast<AAetherCharacter>(UGameplayStatics::GetPlayerPawn(this,0));
            auto* PC=GetWorld()->GetFirstPlayerController();
            if (P && PC)
            {
                const FString Dir=FPaths::ProjectSavedDir()/TEXT("Screenshots/");
                switch (CaptureStage++)
                {
                case 0: FScreenshotRequest::RequestScreenshot(Dir/TEXT("SwordMagic_Gameplay.png"),false,false); break;
                case 1:
                {
                    if (PC->GetHUD()) PC->GetHUD()->bShowHUD=false;
                    const FVector At=P->GetActorLocation(),From=At+FVector(290,-270,90);
                    PC->SetViewTarget(GetWorld()->SpawnActor<ACameraActor>(From,(At-From).Rotation())); break;
                }
                case 2: FScreenshotRequest::RequestScreenshot(Dir/TEXT("SwordMagic_Sword.png"),false,false); break;
                case 3: P->Equipment->Equip(TEXT("TrainingHammer")); break;
                case 4: FScreenshotRequest::RequestScreenshot(Dir/TEXT("SwordMagic_Hammer.png"),false,false); break;
                case 5:
                {
                    const FVector From(-3100,-4600,3900),At(1700,0,100);
                    PC->SetViewTarget(GetWorld()->SpawnActor<ACameraActor>(From,(At-From).Rotation())); break;
                }
                case 6: FScreenshotRequest::RequestScreenshot(Dir/TEXT("SwordMagic_Abbey.png"),false,false); break;
                default: FPlatformMisc::RequestExitWithStatus(false,0); break;
                }
            }
        }
        return;
    }
    if (bCapture && Elapsed>8 && !bCaptured)
    { bCaptured=true; FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots/AetherAdventure.png"),false,false); }
    if (bCapture && Elapsed>10) FPlatformMisc::RequestExitWithStatus(false,0);
}
void AAetherAdventureHUD::DrawHUD()
{
    Super::DrawHUD(); if (!Canvas || !PlayerOwner) return;
    auto* P=Cast<AAetherCharacter>(PlayerOwner->GetPawn()); auto* GS=GetWorld()->GetGameState<AAetherAdventureState>(); if (!P||!GS) return;
    const float W=Canvas->SizeX,H=Canvas->SizeY;
    DrawRect(FLinearColor(.015f,.025f,.045f,.92f),0,0,W,124);
    DrawText(TEXT("EMBER OATH  /  THE BROKEN BELL ABBEY  /  SWORD & MAGIC"),FColor::White,20,12,nullptr,1.3f);
    DrawText(FString::Printf(TEXT("HP %.0f   MANA %.0f   STAMINA %.0f   WATER %.1f kg"),P->Health(),P->Mana(),P->Stamina(),P->WaterReserveKg),FColor::Cyan,20,39);
    const TCHAR* Spells[]={TEXT("1 HEAT"),TEXT("2 WATER"),TEXT("3 FROST"),TEXT("4 LIGHTNING")};
    DrawText(FString::Printf(TEXT("WASD Move | Mouse Aim | LMB Sword | Shift Heavy | RMB Guard | Space Dodge | F Cast [%s]"),Spells[P->SelectedSpell]),FColor::White,20,62);
    const auto* Equipped=P->Equipment->InSlot(TEXT("MainHand"));
    DrawText(FString::Printf(TEXT("R Swap weapon [%s] | T Shield | X Stow | E Use | Q Take sigil | F5 Save | F9 Load"),Equipped?*Equipped->DisplayName.ToString():TEXT("EMPTY")),FColor::Silver,20,84);
    DrawText(P->Feedback,FColor::Yellow,20,105);
    FHitResult AimHit; FVector AimOrigin,AimDirection; P->FindSpellTarget(0,AimHit,AimOrigin,AimDirection);
    FVector2D Crosshair(W/2,H/2);
    PlayerOwner->ProjectWorldLocationToScreen(AimHit.bBlockingHit?AimHit.ImpactPoint:AimOrigin+AimDirection*1800,Crosshair);
    DrawLine(Crosshair.X-8,Crosshair.Y,Crosshair.X+8,Crosshair.Y,FLinearColor::White); DrawLine(Crosshair.X,Crosshair.Y-8,Crosshair.X,Crosshair.Y+8,FLinearColor::White);
    const TCHAR* Goals[]={TEXT("Cross the channel: cut the rope, freeze shallow water, or use the side path."),TEXT("Reach the abbey. Rescue the apprentice and find the oath record."),TEXT("Resolve Olen by sword, environment, or the witness bell."),TEXT("Decide the ancient sigil's fate."),TEXT("Return to the Greyford marker at the entrance."),TEXT("QUEST COMPLETE")};
    DrawRect(FLinearColor(.01f,.02f,.04f,.9f),0,H-73,W,73);
    DrawText(Goals[FMath::Min<int32>(GS->Quest.Phase,5)],FColor::White,20,H-63,nullptr,1.1f);
    DrawText(FString::Printf(TEXT("Apprentice [%s]  Record [%s]  Guardian [%s]  Crowns %d  |  World rules affect everyone."),GS->Quest.bApprentice?TEXT("YES"):TEXT("NO"),GS->Quest.bRecord?TEXT("YES"):TEXT("NO"),GS->Quest.Phase>=3?TEXT("RESOLVED"):TEXT("ACTIVE"),GS->Quest.Crowns),FColor::Silver,20,H-36);
    if (!P->Alive()) DrawText(TEXT("DOWNED / F9 LOAD YOUR SAVE"),FColor::Red,W*.3f,H*.4f,nullptr,2);
}
