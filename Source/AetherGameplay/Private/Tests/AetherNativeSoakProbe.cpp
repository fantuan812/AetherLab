#include "Tests/AetherNativeSoakProbe.h"
#if !UE_BUILD_SHIPPING
#include "Framework/AetherPlayerController.h"
#include "Framework/AetherFrontier.h"
#include "Networking/AetherCommandClient.h"
#include "Presentation/AetherMenuSubsystem.h"
#include "AetherMotionComponent.h"
#include "MotionBricksScheduler.h"
#include "Engine/LocalPlayer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/PlatformMemory.h"
#include "HAL/PlatformTime.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/UObjectIterator.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#endif

void AetherNativeSoakProbe::Tick(AAetherPlayerController* PC,float Dt)
{
#if !UE_BUILD_SHIPPING
    if(!FParse::Param(FCommandLine::Get(),TEXT("AetherV10Soak"))||!PC||!PC->HasAuthority()||!PC->IsLocalController()||!PC->GetLocalPlayer())return;
    struct FState
    {
        double Start=FPlatformTime::Seconds(),ReadyAt=0,Next=0,OperationAt=0,ReportAt=0;
        int32 Phase=-1,Step=0,Replacements=0,Travels=0,Menus=0,SampleIndex=0;
        bool Done=false;FVector Destination;FGuid OldChannel;
        TArray<double> Frames[3];TArray<TSharedPtr<FJsonValue>> Samples;
    };
    static FState S;if(S.Done)return;
    const double Now=FPlatformTime::Seconds();double Duration=3600;FParse::Value(FCommandLine::Get(),TEXT("AetherSoakSeconds="),Duration);
    Duration=FMath::Clamp(Duration,240.,7200.);
    auto* LP=PC->GetLocalPlayer();auto* Client=LP->GetSubsystem<UAetherCommandClient>();auto* Menu=LP->GetSubsystem<UAetherMenuSubsystem>();
    auto* C=Cast<AAetherFrontierCharacter>(PC->GetPawn());auto* Mode=PC->GetWorld()->GetAuthGameMode<AAetherFrontierMode>();
    const auto Fail=[&](const FString& Why){S.Done=true;UE_LOG(LogTemp,Error,TEXT("V10_SOAK_FAIL step=%d replacements=%d travels=%d %s"),S.Step,S.Replacements,S.Travels,*Why);FPlatformMisc::RequestExitWithStatus(false,1);};
    const bool Ready=C&&C->Ready()&&!C->bTravelPending&&Client->GetChannel().IsValid()&&Client->GetProfile().IsSet();
    if(S.ReadyAt==0)
    {
        if(!Ready){if(Now-S.Start>90)Fail(TEXT("Initial native readiness deadline"));return;}
        S.ReadyAt=Now;S.Next=Now+2;S.ReportAt=Now;
    }
    const double Elapsed=Now-S.ReadyAt;
    const int32 Phase=FMath::Min(2,FMath::FloorToInt(Elapsed/(Duration/3)));
    if(S.Phase!=Phase)
    {
        S.Phase=Phase;
        if(auto* Backend=IConsoleManager::Get().FindConsoleVariable(TEXT("aether.Motion.Backend")))Backend->Set(Phase,ECVF_SetByCode);
        UE_LOG(LogTemp,Display,TEXT("V10_SOAK_BACKEND %d elapsed=%.1f"),Phase,Elapsed);
    }
    // 帧样本有固定上限：持续一小时也不会产生逐帧增长的探针内存。
    if(S.Frames[Phase].Num()<250000)S.Frames[Phase].Add(Dt*1000.);
    if(Now>=S.ReportAt)
    {
        S.ReportAt=Now+30;
        int32 Pawns=0,Previews=0,Targets=0,Widgets=0;
        for(TObjectIterator<UObject> It;It;++It)
        {
            if(It->HasAnyFlags(RF_ClassDefaultObject)||!IsValid(*It))continue;
            if(It->IsA<AAetherFrontierCharacter>()&&It->GetWorld()==PC->GetWorld())++Pawns;
            const FString Name=It->GetClass()->GetName();
            if(Name==TEXT("AetherCharacterPreviewActor"))++Previews;
            if(It->IsA<UTextureRenderTarget2D>())++Targets;
            if(Name.StartsWith(TEXT("WBP_")))++Widgets;
        }
        const auto Metrics=AetherMotionScheduler().Inspect();
        const auto Memory=FPlatformMemory::GetStats();auto Sample=MakeShared<FJsonObject>();
        Sample->SetNumberField(TEXT("seconds"),Elapsed);Sample->SetNumberField(TEXT("backend"),Phase);
        Sample->SetNumberField(TEXT("physicalBytes"),double(Memory.UsedPhysical));Sample->SetNumberField(TEXT("peakPhysicalBytes"),double(Memory.PeakUsedPhysical));
        Sample->SetNumberField(TEXT("pawns"),Pawns);Sample->SetNumberField(TEXT("previews"),Previews);
        Sample->SetNumberField(TEXT("renderTargets"),Targets);Sample->SetNumberField(TEXT("widgets"),Widgets);
        Sample->SetNumberField(TEXT("agents"),Metrics.Agents);Sample->SetNumberField(TEXT("nativeAgents"),Metrics.NativeAgents);
        Sample->SetNumberField(TEXT("pending"),Metrics.Pending);Sample->SetNumberField(TEXT("executing"),Metrics.Executing);
        Sample->SetNumberField(TEXT("nativeCalls"),double(Metrics.NativeCalls));Sample->SetNumberField(TEXT("nativeFailures"),double(Metrics.NativeFailures));
        S.Samples.Add(MakeShared<FJsonValueObject>(Sample));
        UE_LOG(LogTemp,Display,TEXT("V10_SOAK_SAMPLE seconds=%.1f phase=%d pawns=%d previews=%d rt=%d agents=%d native=%d calls=%llu memory_mb=%.1f"),Elapsed,Phase,Pawns,Previews,Targets,Metrics.Agents,Metrics.NativeAgents,Metrics.NativeCalls,double(Memory.UsedPhysical)/1048576.);
        if(Metrics.Agents>16||Metrics.NativeAgents>16||Metrics.Pending>Metrics.Agents||Metrics.Executing>1){Fail(TEXT("Native resource/queue bound violated"));return;}
    }
    if(Elapsed>=Duration&&S.Step==0)
    {
        if(S.Replacements<50||S.Travels<50||S.Menus<100){Fail(TEXT("Required lifecycle cycles incomplete"));return;}
        Menu->Close();auto Report=MakeShared<FJsonObject>();Report->SetNumberField(TEXT("schema"),1);
        Report->SetNumberField(TEXT("seconds"),Elapsed);Report->SetNumberField(TEXT("replacements"),S.Replacements);
        Report->SetNumberField(TEXT("regionTravels"),S.Travels);Report->SetNumberField(TEXT("menuCycles"),S.Menus);
        Report->SetBoolField(TEXT("shipping"),false);Report->SetBoolField(TEXT("fullMainline"),false);
        Report->SetBoolField(TEXT("longDurationSatisfied"),Elapsed>=3600);
        Report->SetStringField(TEXT("scope"),TEXT("Native standalone rendered lifecycle: movement, menus, safe travel, Pawn replacement; full quest playthrough and GPU memory are separate."));
        TArray<TSharedPtr<FJsonValue>> Timings;
        for(int32 I=0;I<3;++I){auto Values=S.Frames[I];Values.Sort();auto Row=MakeShared<FJsonObject>();Row->SetNumberField(TEXT("backend"),I);Row->SetNumberField(TEXT("samples"),Values.Num());for(const auto P:{50,95,99})if(Values.Num())Row->SetNumberField(FString::Printf(TEXT("p%dFrameMs"),P),Values[FMath::Clamp(FMath::CeilToInt(Values.Num()*P/100.)-1,0,Values.Num()-1)]);Timings.Add(MakeShared<FJsonValueObject>(Row));}
        Report->SetArrayField(TEXT("frameTimings"),Timings);Report->SetArrayField(TEXT("resources"),S.Samples);
        FString Json,Path;FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Json));FParse::Value(FCommandLine::Get(),TEXT("AetherSoakReport="),Path);
        if(Path.IsEmpty()||!FFileHelper::SaveStringToFile(Json,*Path)){Fail(TEXT("Cannot save isolated evidence report"));return;}
        S.Done=true;UE_LOG(LogTemp,Display,TEXT("V10_SOAK_PASS seconds=%.1f replacements=%d travels=%d menus=%d"),Elapsed,S.Replacements,S.Travels,S.Menus);FPlatformMisc::RequestExit(false);return;
    }
    if(Ready&&S.Step==0&&!Menu->IsOpen())C->AddMovementInput(FVector(FMath::Cos(Elapsed),FMath::Sin(Elapsed),0),.25);
    if(S.Step!=0&&Now-S.OperationAt>55){Fail(TEXT("Lifecycle operation failed to become ready"));return;}
    if(Now<S.Next||!Mode)return;
    if(S.Step==1)
    {
        if(!Ready)return;
        if(Client->GetChannel()==S.OldChannel){Fail(TEXT("Replacement reused old native channel"));return;}
        ++S.Replacements;S.Step=0;S.Next=Now+.25;return;
    }
    if(S.Step==2)
    {
        if(!Ready)return;
        if(FVector::DistSquared(C->GetActorLocation(),S.Destination)>FMath::Square(1200.)){Fail(TEXT("Safe travel cancelled or landed in wrong region"));return;}
        ++S.Travels;S.Step=0;S.Next=Now+.25;return;
    }
    if(!Ready){S.Next=Now+.25;return;}
    if(S.Replacements<50&&S.Replacements<=S.Travels&&Elapsed>=S.Replacements*Duration/60.)
    {
        Menu->OpenPage(EAetherMenuPage::Inventory);S.OldChannel=Client->GetChannel();
        C->ReleaseHeldInput();PC->UnPossess();C->Destroy();Mode->RestartPlayer(PC);
        S.Step=1;S.OperationAt=Now;S.Next=Now+.5;return;
    }
    if(S.Travels<S.Replacements)
    {
        Menu->Close();S.Destination=S.Travels%2?FVector(-6500,-29000,120):FVector(-500,-500,120);
        C->BeginSafeTravel(S.Destination);S.Step=2;S.OperationAt=Now;S.Next=Now+.5;return;
    }
    // 稳定阶段在真实渲染地图持续触发不同移动意图与菜单，不反复生成测试世界。
    if(S.SampleIndex++%2==0)Menu->OpenPage(EAetherMenuPage(1+(S.SampleIndex/2)%6));
    else {Menu->Close();++S.Menus;C->SetCrouchInput((S.SampleIndex/2)%3==0);C->StartJumpInput();}
    S.Next=Now+.5;
#endif
}
