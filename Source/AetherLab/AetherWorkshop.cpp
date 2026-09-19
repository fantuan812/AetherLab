#include "AetherFrontier.h"
#include "AetherTraversal.h"
#include "AetherGuide.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "ReactiveWorldSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Sound/SoundWaveProcedural.h"
#include "Kismet/GameplayStatics.h"
#include "UnrealClient.h"
#include "Misc/Paths.h"
#include "GameFramework/PlayerController.h"

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
    if(FParse::Param(FCommandLine::Get(),TEXT("AetherV10TradeCapture")))
    {
        // 独立开发夹具使用脚本生成的新存档前缀；覆盖实际 UMG、授权和确认显示。
        if(SmokeStage==0&&Elapsed>3&&C->ProfileState())
        {
            C->SetActorLocation(FVector(-400,150,130));PC->SetControlRotation(FRotator(0,90,0));
            UpdateRegions({C->GetActorLocation()});if(!Prop("Shop"))return;
            C->ResetCombat();auto P=C->ProfileState()->Profile;P.Gold=100;P.Inventory.Reset();P.Equipped.Reset();P.Add("Potion",3);
            if(!Commit(C->ProfileState(),P)){FPlatformMisc::RequestExitWithStatus(false,1);return;}
            C->SelectedInstance=C->ProfileState()->Profile.Inventory[0].InstanceId;C->InventoryQuantity=2;SmokeStage=1;
        }
        if(SmokeStage==1&&Elapsed>6)
        {
            auto* Shop=Prop("Shop");if(!Shop)return;
            const auto Selection=AetherGuide::QueryTarget(C,Shop);InteractTarget(C,Selection);
            if(C->ActiveShop().IsNone()){UE_LOG(LogTemp,Error,TEXT("AETHER_TRADE_CAPTURE_FAIL open"));FPlatformMisc::RequestExitWithStatus(false,1);return;}
            SmokeStage=2;
        }
        if(SmokeStage==2&&Elapsed>8){FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Automation/V10Trade-Open.png"),true,false);SmokeStage=3;}
        if(SmokeStage==3&&Elapsed>10){C->RequestSale();if(C->SaleConfirmationText().IsEmpty()){FPlatformMisc::RequestExitWithStatus(false,1);return;}SmokeStage=4;}
        if(SmokeStage==4&&Elapsed>11){FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Automation/V10Trade-Sale.png"),true,false);SmokeStage=5;}
        if(SmokeStage==5&&Elapsed>12){C->SetActorLocation(FVector(-400,900,130));C->MaintainTrade();SmokeStage=6;}
        if(SmokeStage==6&&Elapsed>14){FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Automation/V10Trade-Closed.png"),true,false);SmokeStage=7;}
        if(SmokeStage==7&&Elapsed>16)
        {
            C->SetActorLocation(FVector(-400,150,130));C->ResetCombat();if(!C->OpenTrade(Prop("Shop"))){FPlatformMisc::RequestExitWithStatus(false,1);return;}
            bFailWrites=true;C->SubmitInventory("Buy","Potion");bFailWrites=false;
            if(!C->PendingInventory.CommandId.IsValid()){FPlatformMisc::RequestExitWithStatus(false,1);return;}
            C->SetActorLocation(FVector(-400,900,130));C->MaintainTrade();SmokeStage=8;
        }
        if(SmokeStage==8&&Elapsed>18){FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Automation/V10Trade-Retry.png"),true,false);SmokeStage=9;}
        if(SmokeStage==9&&Elapsed>20)
        {
            // 与 UI 的“重试上次操作”使用同一入口；无提交的失效会话请求会明确拒绝并释放挂起状态。
            C->SubmitInventory(NAME_None);
            const bool Passed=C->ActiveShop().IsNone()&&!C->PendingInventory.CommandId.IsValid()&&C->ProfileState()->Profile.Count("Potion")==3&&C->ProfileState()->Profile.Gold==100;
            UE_LOG(LogTemp,Display,TEXT("AETHER_TRADE_CAPTURE_%s"),Passed?TEXT("PASS"):TEXT("FAIL"));SmokeStage=10;FPlatformMisc::RequestExitWithStatus(false,Passed?0:1);
        }
        return;
    }
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
