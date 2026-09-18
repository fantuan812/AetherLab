#include "AetherFrontier.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "ReactiveWorldSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/PlatformMisc.h"

void AAetherFrontierMode::SmokeStep()
{
    if(Elapsed<2)return;auto* C=Cast<AAetherFrontierCharacter>(UGameplayStatics::GetPlayerPawn(this,0));if(!C||!C->ProfileState())return;
    auto* PS=C->ProfileState();
    auto Check=[&](bool Pass,const TCHAR* Name){UE_LOG(LogTemp,Display,TEXT("V4_CHECK %s %s"),Pass?TEXT("PASS"):TEXT("FAIL"),Name);if(!Pass)++Failures;};
    if(SmokeStage==0)
    {
        Check(C->AbilitySystem==PS->AbilitySystem,TEXT("ASC owned by PlayerState"));
        Check(C->AbilitySystem->GetActivatableAbilities().Num()==4,TEXT("Persistent ASC receives exactly four specs"));
        Check(Prop("WorksWater0")->Mesh->GetCollisionResponseToChannel(ECC_Pawn)==ECR_Ignore,TEXT("Liquid channel is not a solid floor"));
        Check(C->GetMesh()->GetSkeletalMeshAsset()&&C->GetMesh()->GetSkeletalMeshAsset()->GetPathName().Contains("Mannequins"),TEXT("Official mannequin loaded"));
        for(FName Id:{FName("SupplyA"),FName("SupplyB"),FName("Gate"),FName("Registrar"),FName("Inn")}){C->SetActorLocation(Prop(Id)->GetActorLocation()+FVector(-130,0,10));Interact(C);}
        Check(PS->Profile.Claims.Contains(FName("Q_Main_02")),TEXT("Arrival registration rewards"));
        Check(PS->Profile.Count("Supply")==2&&PS->Profile.Count("TrainingSword")==1,TEXT("Inventory and one-time issue"));
        const int32 Gold=PS->Profile.Gold;Interact(C);Check(PS->Profile.Gold==Gold,TEXT("Repeated interaction does not duplicate reward"));
        auto Failed=PS->Profile;Failed.Gold+=123;bFailWrites=true;Check(!Commit(PS,Failed)&&PS->Profile.Gold==Gold,TEXT("Write failure does not publish assets"));bFailWrites=false;
        C->SetActorLocation(Prop("Teacher")->GetActorLocation()+FVector(-130,0,0));Interact(C);
        Check(C->SpellUnlocked(0)&&C->SpellUnlocked(1)&&!C->SpellUnlocked(2),TEXT("Learning gates"));
        for(FName F:{FName("Melee1"),FName("Melee2"),FName("Melee3"),FName("Block"),FName("TrainingExtinguished")})Observe(C,F);
        Check(PS->Profile.Available("Q_Main_04")&&PS->Profile.Available("Q_Main_05"),TEXT("Both field quests available independently"));
        C->SetActorLocation(Prop("Pump")->GetActorLocation()+FVector(-130,0,20));Interact(C);
        Check(PS->Profile.Claims.Contains(FName("Q_Main_05")),TEXT("Mechanical supply route needs no lightning"));
        for(int32 I=0;I<3;++I){auto* Fire=Prop(*FString::Printf(TEXT("ForestFire%d"),I));FReactiveStimulus Water;Water.SourceActor=C;Water.WaterKg=.5;Fire->Reactive->Inject(Water);}
        SmokeStage=1;return;
    }
    if(SmokeStage==1&&Elapsed>3)
    {
        for(int32 I=0;I<3;++I)Check(PS->Profile.Evidence.Contains(*FString::Printf(TEXT("ForestFire%d"),I)),TEXT("Actual extinguish event credited"));
        Observe(C,"Rescue");Check(PS->Profile.Available("Q_Main_06"),TEXT("Both field prerequisites required"));
        C->SetActorLocation(Prop("Recruit")->GetActorLocation()+FVector(-130,0,0));RecruitCompanion(C);
        Check(PS->Profile.Claims.Contains(FName("Q_Main_06"))&&Companions.Num()==1,TEXT("Companion recruitment"));
        C->SetActorLocation(Prop("AbbeyEntry")->GetActorLocation()+FVector(-130,0,0));Interact(C);
        Check(IsValid(Guardian),TEXT("Encounter starts with human and AI"));
        if(Guardian){CreditHit(Guardian,C);Guardian->SetVitals(0,0,0);}
        FReactiveStimulus Cut;Cut.SourceActor=C;Cut.ImpulseNs=FVector(100,0,0);Prop("WorksRope")->Reactive->Inject(Cut);
        auto* Rod=Prop("MetalRod");Rod->Mesh->SetSimulatePhysics(false);Rod->SetActorLocation(FVector(28350,650,40));
        SmokeStage=2;return;
    }
    if(SmokeStage==2&&Elapsed>4)
    {
        Check(PS->Profile.Count("AncientSeal")==1,TEXT("Participant receives seal once"));
        C->SetActorLocation(Prop("Steward")->GetActorLocation()+FVector(-130,0,0));Interact(C);
        Check(PS->Profile.Claims.Contains(FName("Q_Main_08")),TEXT("Main quest completed"));
        Check(GetGameState<AAetherFrontierState>()->bBridgeReleased&&Prop("WorksBridge")->Mesh->IsSimulatingPhysics(),TEXT("Cut support releases physical bridge"));
        const auto* Saved=Database->Profiles.FindByPredicate([&](const auto& P){return P.CharacterId==PS->Profile.CharacterId;});
        Check(Saved&&Saved->Claims==PS->Profile.Claims,TEXT("Durable profile matches live state"));
        auto* Disk=Cast<UAetherFrontierSave>(UGameplayStatics::LoadGameFromSlot(SavePrefix+FString::FromInt(Database->Generation%2),0));
        Check(Disk&&Disk->Profiles.Num()==1&&Disk->Profiles[0].Claims.Contains(FName("Q_Main_08")),TEXT("Save roundtrip retains final claim"));
        Check(Prop("PowerReceiver")->ReceivedPower>1,TEXT("Moved conductor creates real powered contact"));
        GetGameState<AAetherFrontierState>()->bPowerOn=false;SmokeStage=3;return;
    }
    if(SmokeStage==3&&Elapsed>5)
    {
        Check(Prop("PowerReceiver")->ReceivedPower==0,TEXT("Disconnecting source clears power without residual shock"));
        Check(SaveWorld(),TEXT("World snapshot saves with profiles"));
        const auto Records=Database->World;
        auto* Rod=Prop("MetalRod");Rod->SetActorLocation(FVector(26500,500,80));
        Check(GetWorld()->GetSubsystem<UReactiveWorldSubsystem>()->Restore(Records),TEXT("World snapshot restores atomically"));
        Check(Rod->GetActorLocation().Equals(FVector(28350,650,40),1),TEXT("Conductor transform restored"));
        UE_LOG(LogTemp,Display,TEXT("AETHER_V4_SMOKE_%s checks_failed=%d"),Failures?TEXT("FAIL"):TEXT("PASS"),Failures);
        SmokeStage=4;FPlatformMisc::RequestExitWithStatus(false,Failures?1:0);
    }
}

