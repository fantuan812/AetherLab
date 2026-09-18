#include "AetherFrontier.h"
#include "AetherTraversal.h"
#include "ReactiveWorldSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Sound/SoundWaveProcedural.h"
#include "Kismet/GameplayStatics.h"
#include "UnrealClient.h"
#include "Misc/Paths.h"
#include "GameFramework/PlayerController.h"

void AAetherFrontierMode::BuildWorkshop()
{
    // A second authored slice within the existing town; shared service/material definitions.
    auto Add=[&](FName Id,FName Service,FVector P,FVector Scale,EAetherObjectKind Kind,const TCHAR* Label)
    {
        auto* A=Make(Id,Service,P+FVector(6000,5000,0),Scale,Kind,Label);
        A->Reactive->bAllowAbsentFromOlderSave=true;
        A->bWorkshopService=Service=="Source"||Service=="Receiver"||Service=="SupplyRestored";
        return A;
    };
    Add("LabBoard",NAME_None,{-600,-700,100},{.1,.1,.1},EAetherObjectKind::Stone,TEXT("REACTION WORKSHOP\nRESTORE POWER OR USE MANUAL PUMP\nMISSING MATERIAL? USE BYPASS\nOUT OF WATER? REST AT TOWN INN"));
    Add("LabDummy","Dummy",{-600,-350,90},{.7,.7,1.8},EAetherObjectKind::Stone,TEXT("TRAINING SWORD / CUT ROPE OR STRIKE DUMMY"));
    Add("LabFire","WorkshopFire",{-600,300,50},{.9,.9,1},EAetherObjectKind::Timber,TEXT("FIRE / WATER OR BUCKET"));
    Add("LabBucket","Bucket",{-600,550,45},{.6,.6,.9},EAetherObjectKind::Water,TEXT("E / FINITE BUCKET"));
    Add("LabWell","Well",{-1000,600,50},{1,1,1},EAetherObjectKind::Cistern,TEXT("E / FINITE RESERVOIR / INN REST REPLENISHES SPELL WATER"));
    Add("LabCrate","Crate",{-250,-650,50},{.8,.8,1},EAetherObjectKind::Timber,TEXT("G CARRY / V PUSH / THROW / FLOAT"));
    Add("LabWest",NAME_None,{-300,0,120},{3,10,2.4},EAetherObjectKind::Stone,TEXT(""));
    for(int I=0;I<3;++I)Add(*FString::Printf(TEXT("LabStep%d"),I),NAME_None,{-700.f+I*100,0,30.f+I*30},{1,3,.6f+I*.6f},EAetherObjectKind::Stone,TEXT(""));
    Add("LabEast",NAME_None,{900,0,120},{3,10,2.4},EAetherObjectKind::Stone,TEXT(""));
    Add("LabBypass",NAME_None,{300,650,120},{15,3,2.4},EAetherObjectKind::Stone,TEXT("MAINTENANCE BYPASS / NO MAGIC OR MATERIALS REQUIRED"));
    auto* Rope=Add("LabRope","Support",{-400,-200,350},{.2,.2,2.2},EAetherObjectKind::Rope,TEXT("CUT OR BURN SUPPORT"));
    auto* Bridge=Add("LabBridge","Bridge",{300,-250,650},{9,3,.25},EAetherObjectKind::Stone,TEXT("WAIT FOR BRIDGE TO SETTLE"));
    Bridge->Mechanism->Supports.Add(Rope->Reactive);Bridge->Mesh->SetMassOverrideInKg(NAME_None,60,true);
    Add("LabStopA",NAME_None,{-80,-250,210},{.4,3,.6},EAetherObjectKind::Stone,TEXT(""));
    Add("LabStopB",NAME_None,{680,-250,210},{.4,3,.6},EAetherObjectKind::Stone,TEXT(""));
    for(int I=0;I<3;++I)Add(*FString::Printf(TEXT("LabWater%d"),I),"Water",{float(I*300),150,228},{3,3,.2},EAetherObjectKind::Water,TEXT("FROST PATH / WATCH THAW WARNING"));
    Add("LabSource","Source",{1400,200,40},{1,1,.8},EAetherObjectKind::Stone,TEXT("E / INDEPENDENT FINITE SOURCE"));
    Add("LabReceiver","Receiver",{1700,200,40},{1,1,.8},EAetherObjectKind::Stone,TEXT("E / RESTORE WORKSHOP WITH POWER"));
    Add("LabRod","Conductor",{1550,-150,40},{2.1,.3,.3},EAetherObjectKind::Stone,TEXT("G / MOVE ROD BETWEEN TERMINALS"));
    Add("LabPump","SupplyRestored",{1100,650,80},{1,1,1.6},EAetherObjectKind::Stone,TEXT("E / MANUAL RESTORE / NO POWER NEEDED"));
}
void AAetherFrontierProp::UpdateReactionFeedback()
{
    if(GetNetMode()==NM_DedicatedServer||!Spec.bInteractiveMaterial)return;
    const auto& S=Reactive->State;uint8 Feedback=0;FString Text;FLinearColor Color=Spec.Color;
    if(S.bBroken){Feedback=1;Text=TEXT("BROKEN / BYPASS AVAILABLE");Color=FLinearColor(.12,.08,.05);}
    else if(S.bBurning){Feedback=2;Text=TEXT("BURNING");Color=FLinearColor(1,.09,.005);}
    else if(Reactive->IceSupport==EReactiveIceSupport::Thawing){Feedback=3;Text=TEXT("THAWING / LEAVE ICE NOW");Color=FLinearColor(1,.38,.04);}
    else if(Reactive->IceSupport==EReactiveIceSupport::FreezePending){Feedback=4;Text=TEXT("FREEZING / CLEAR SURFACE");Color=FLinearColor(.25,.5,.7);}
    else if(Reactive->IceSupport==EReactiveIceSupport::Bearing){Feedback=5;Text=TEXT("ICE / SUPPORT ACTIVE");Color=FLinearColor(.5,.85,1);}
    else if(ReceivedPower>1){Feedback=6;Text=FString::Printf(TEXT("ENERGIZED / %.0f W"),ReceivedPower);Color=FLinearColor(.2,1,.65);}
    else if(Reactive->bElectricalContact){Feedback=7;Text=TEXT("CONTACT / NO USEFUL POWER");Color=FLinearColor(.3,.55,.8);}
    else if(bExtinguished){Feedback=8;Text=TEXT("EXTINGUISHED");Color=FLinearColor(.1,.25,.28);}
    else if(S.Integrity<.8){Feedback=9;Text=TEXT("SUPPORT WEAKENING");Color=FLinearColor(.7,.4,.08);}
    if(Service=="Bridge")Text=Traversal->bRouteOpen?TEXT("BRIDGE / WALKABLE"):TEXT("BRIDGE / UNSTABLE");
    if(Service=="Source")Text=Mechanism->bPowerEnabled?TEXT("SOURCE ON / FINITE ENERGY"):TEXT("SOURCE OFF");
    if(bWorkshopService)if(const auto* State=GetWorld()->GetGameState<AAetherFrontierState>();State&&State->bWorkshopRestored)Text+=TEXT(" / WORKSHOP RESTORED");
    if(auto* Mat=Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0)))Mat->SetVectorParameterValue(TEXT("Color"),Color);
    Label->SetWorldSize(12);
    if(auto* Viewer=GetWorld()->GetFirstPlayerController();Viewer&&Viewer->GetPawn())Label->SetVisibility(FVector::DistSquared(Viewer->GetPawn()->GetActorLocation(),GetActorLocation())<FMath::Square(600.));
    Label->SetText(FText::FromString(Spec.Label+(Text.IsEmpty()?TEXT(""):TEXT("\n")+Text)));
    // Short locally generated cues; no imported art/audio and no duplicated server sound events.
    auto* PC=GetWorld()->GetFirstPlayerController();
    if(LastFeedback!=255&&LastFeedback!=Feedback&&Feedback&&PC&&PC->GetPawn()&&FVector::DistSquared(PC->GetPawn()->GetActorLocation(),GetActorLocation())<FMath::Square(900.))
    {
        auto* Sound=NewObject<USoundWaveProcedural>(this);Sound->SetSampleRate(16000);Sound->NumChannels=1;Sound->Duration=.12f;
        TArray<int16> PCM;PCM.SetNumUninitialized(1920);const float Hz=Feedback==3?880.f:Feedback==1?140.f:440.f;
        for(int I=0;I<PCM.Num();++I)PCM[I]=int16(1600.f*(1.f-float(I)/PCM.Num())*FMath::Sin(2*PI*Hz*I/16000));
        Sound->QueueAudio(reinterpret_cast<const uint8*>(PCM.GetData()),PCM.Num()*sizeof(int16));
        UGameplayStatics::PlaySoundAtLocation(this,Sound,GetActorLocation(),.3f);
    }
    LastFeedback=Feedback;
}
void AAetherFrontierMode::CaptureWorkshop()
{
#if !UE_BUILD_SHIPPING
    auto* PC=GetWorld()->GetFirstPlayerController();auto* C=PC?Cast<AAetherFrontierCharacter>(PC->GetPawn()):nullptr;
    if(!C)return;
    if(SmokeStage==0&&Elapsed>3)
    {C->SetActorLocation(FVector(5220,5550,110));PC->SetControlRotation(FRotator(-8,-40,0));SmokeStage=1;}
    if(SmokeStage==1&&Elapsed>6){FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Automation/V806-Interaction.png"),true,false);SmokeStage=2;}
    if(SmokeStage==2&&Elapsed>8){C->bPanel=true;C->Panel=1;SmokeStage=3;}
    if(SmokeStage==3&&Elapsed>9){FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Automation/V806-Inventory.png"),true,false);SmokeStage=4;}
    if(SmokeStage==4&&Elapsed>11){C->Panel=2;SmokeStage=5;}
    if(SmokeStage==5&&Elapsed>12){FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Automation/V806-Quests.png"),true,false);SmokeStage=6;}
    if(Elapsed>15)FPlatformMisc::RequestExit(false);
#endif
}
