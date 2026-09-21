#include "Tests/AetherNativeSoakProbe.h"
#if !UE_BUILD_SHIPPING && !UE_SERVER
#include "Framework/AetherPlayerController.h"
#include "Framework/AetherFrontier.h"
#include "Networking/AetherCommandClient.h"
#include "Networking/AetherCommandRuntime.h"
#include "Engine/GameInstance.h"
#include "Contracts/AetherTransaction.h"
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
#include "Framework/Application/SlateApplication.h"
#endif

void AetherNativeSoakProbe::Tick(AAetherPlayerController* PC,float Dt)
{
#if !UE_BUILD_SHIPPING && !UE_SERVER
    if(!FParse::Param(FCommandLine::Get(),TEXT("AetherV10Soak"))||!PC||!PC->HasAuthority()||!PC->IsLocalController()||!PC->GetLocalPlayer())return;
    struct FState
    {
        double Start=FPlatformTime::Seconds(),ReadyAt=0,Next=0,OperationAt=0,ReportAt=0;
        int32 Phase=-1,Step=0,Replacements=0,Travels=0,Menus=0,SampleIndex=0,CommandCycles=0;
        FGuid FavoriteItem;bool FavoriteValue=false;
        bool Done=false;FVector Destination;FGuid OldChannel;
        TArray<double> Frames[3],Bridge[3],Latency[3],Inference[3],SlateOpen[3],SlateClosed[3];
        double SlateAt=0;bool SlateBound=false;TWeakObjectPtr<UAetherMotionComponent> LastMotion;uint64 LastSequence=0;
        TArray<TSharedPtr<FJsonValue>> Samples;
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
    if(!S.SlateBound&&FSlateApplication::IsInitialized()){
        S.SlateBound=true;
        FSlateApplication::Get().OnPreTick().AddWeakLambda(PC,[](float){S.SlateAt=FPlatformTime::Seconds();});
        FSlateApplication::Get().OnPostTick().AddWeakLambda(PC,[Menu](float){
            if(S.Done||S.Phase<0)return;auto& Values=Menu->IsOpen()?S.SlateOpen[S.Phase]:S.SlateClosed[S.Phase];
            if(Values.Num()<250000)Values.Add((FPlatformTime::Seconds()-S.SlateAt)*1000.);
        });
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
    if(C&&C->Motion){
        auto* Motion=C->Motion.Get();const double Bridge=GFrameCounter?Motion->BridgeMilliseconds(GFrameCounter-1):0;
        if(Bridge>0&&S.Bridge[Phase].Num()<250000)S.Bridge[Phase].Add(Bridge);
        if(Motion!=S.LastMotion.Get()){S.LastMotion=Motion;S.LastSequence=0;}
        if(Motion->AcceptedPlanSequence()&&Motion->AcceptedPlanSequence()!=S.LastSequence){
            S.LastSequence=Motion->AcceptedPlanSequence();
            if(S.Latency[Phase].Num()<100000)S.Latency[Phase].Add(Motion->AcceptedPlanMilliseconds());
            if(auto Clip=Motion->PoseClip();Clip&&S.Inference[Phase].Num()<100000)S.Inference[Phase].Add(Clip->InferenceSeconds*1000.);
        }
    }
    if(Now>=S.ReportAt)
    {
        S.ReportAt=Now+30;
        int32 Pawns=0,Previews=0,Targets=0,Widgets=0,AbilitySystems=0,Effects=0,Abilities=0;
        for(TObjectIterator<UObject> It;It;++It)
        {
            if(It->HasAnyFlags(RF_ClassDefaultObject)||!IsValid(*It))continue;
            if(It->IsA<AAetherFrontierCharacter>()&&It->GetWorld()==PC->GetWorld())++Pawns;
            if(auto* ASC=Cast<UAbilitySystemComponent>(*It);ASC&&ASC->GetWorld()==PC->GetWorld())
            {++AbilitySystems;Effects+=ASC->GetNumActiveGameplayEffects();Abilities+=ASC->GetActivatableAbilities().Num();}
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
        const auto Commands=PC->GetGameInstance()->GetSubsystem<UAetherCommandRuntime>()->Inspect();
        Sample->SetNumberField(TEXT("abilitySystems"),AbilitySystems);Sample->SetNumberField(TEXT("activeEffects"),Effects);Sample->SetNumberField(TEXT("abilitySpecs"),Abilities);
        Sample->SetNumberField(TEXT("clientPendingCommands"),Client->PendingCommandCount());
        Sample->SetNumberField(TEXT("serverPendingCommands"),Commands.PendingCommands);Sample->SetNumberField(TEXT("serverPendingFacts"),Commands.PendingFacts);
        Sample->SetNumberField(TEXT("deferredFacts"),Commands.DeferredFacts);Sample->SetNumberField(TEXT("resourceReservations"),Commands.ResourceReservations);
        Sample->SetNumberField(TEXT("connections"),Commands.Connections);Sample->SetNumberField(TEXT("snapshotBytes"),double(Commands.SnapshotBytes));
        if(Commands.PendingCommands>128||Commands.PendingFacts>128||Commands.DeferredFacts>1024||Client->PendingCommandCount()>16){Fail(TEXT("Command queue bound violated"));return;}
        S.Samples.Add(MakeShared<FJsonValueObject>(Sample));
        UE_LOG(LogTemp,Display,TEXT("V10_SOAK_SAMPLE seconds=%.1f phase=%d pawns=%d previews=%d rt=%d agents=%d native=%d calls=%llu memory_mb=%.1f"),Elapsed,Phase,Pawns,Previews,Targets,Metrics.Agents,Metrics.NativeAgents,Metrics.NativeCalls,double(Memory.UsedPhysical)/1048576.);
        if(Metrics.Agents>16||Metrics.NativeAgents>16||Metrics.Pending>Metrics.Agents||Metrics.Executing>1){Fail(TEXT("Native resource/queue bound violated"));return;}
    }
    if(Elapsed>=Duration&&S.Step==0)
    {
        if(S.Replacements<50||S.Travels<50||S.Menus<100||S.CommandCycles<50||Client->PendingCommandCount()!=0){Fail(TEXT("Required lifecycle cycles incomplete"));return;}
        Menu->Close();auto Report=MakeShared<FJsonObject>();Report->SetNumberField(TEXT("schema"),1);
        Report->SetNumberField(TEXT("seconds"),Elapsed);Report->SetNumberField(TEXT("replacements"),S.Replacements);
        Report->SetNumberField(TEXT("regionTravels"),S.Travels);Report->SetNumberField(TEXT("commandCycles"),S.CommandCycles);Report->SetNumberField(TEXT("menuCycles"),S.Menus);
        Report->SetBoolField(TEXT("shipping"),false);Report->SetBoolField(TEXT("fullMainline"),false);
        Report->SetBoolField(TEXT("longDurationSatisfied"),Elapsed>=3600);
        Report->SetStringField(TEXT("scope"),TEXT("Native standalone rendered lifecycle: movement, menus, safe travel, Pawn replacement; full quest playthrough and GPU memory are separate."));
        TArray<TSharedPtr<FJsonValue>> Timings;
        for(int32 I=0;I<3;++I){auto Values=S.Frames[I];Values.Sort();auto Row=MakeShared<FJsonObject>();Row->SetNumberField(TEXT("backend"),I);Row->SetNumberField(TEXT("samples"),Values.Num());for(const auto P:{50,95,99})if(Values.Num())Row->SetNumberField(FString::Printf(TEXT("p%dFrameMs"),P),Values[FMath::Clamp(FMath::CeilToInt(Values.Num()*P/100.)-1,0,Values.Num()-1)]);Timings.Add(MakeShared<FJsonValueObject>(Row));}
        Report->SetArrayField(TEXT("frameTimings"),Timings);
        TArray<TSharedPtr<FJsonValue>> Performance;
        const auto Summarize=[](TArray<double> Values){auto O=MakeShared<FJsonObject>();Values.Sort();O->SetNumberField(TEXT("samples"),Values.Num());for(const int32 P:{50,95,99})if(Values.Num())O->SetNumberField(FString::Printf(TEXT("p%dMs"),P),Values[FMath::Clamp(FMath::CeilToInt(Values.Num()*P/100.)-1,0,Values.Num()-1)]);return O;};
        for(int32 I=0;I<3;++I){auto O=MakeShared<FJsonObject>();O->SetNumberField(TEXT("backend"),I);
            O->SetObjectField(TEXT("bridgeGameThread"),Summarize(S.Bridge[I]));O->SetObjectField(TEXT("acceptedPlanEndToEnd"),Summarize(S.Latency[I]));
            O->SetObjectField(TEXT("acceptedPlanInference"),Summarize(S.Inference[I]));
            O->SetObjectField(TEXT("slateMenuOpenTotal"),Summarize(S.SlateOpen[I]));O->SetObjectField(TEXT("slateMenuClosedTotal"),Summarize(S.SlateClosed[I]));
            Performance.Add(MakeShared<FJsonValueObject>(O));}
        Report->SetArrayField(TEXT("performance"),Performance);
        Report->SetStringField(TEXT("timingScope"),TEXT("Bridge includes component, source PreUpdate, body NativeUpdate and retarget proxy PreUpdate. Plan latency includes queue/load/inference/game-thread pickup, including cold starts. Slate totals are conservative whole-application tick costs, not isolated widget cost."));
Report->SetArrayField(TEXT("resources"),S.Samples);
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
    if(S.Step==3)
    {
        if(!Ready||Client->HasPending())return;
        const auto* Item=Client->GetProfile()->Inventory.Find(S.FavoriteItem);
        if(!Item||Item->bFavorite!=S.FavoriteValue){Fail(TEXT("Lifecycle command did not publish"));return;}
        ++S.CommandCycles;Menu->OpenPage(EAetherMenuPage::Inventory);S.OldChannel=Client->GetChannel();
        C->ReleaseHeldInput();PC->UnPossess();C->Destroy();Mode->RestartPlayer(PC);
        S.Step=1;S.OperationAt=Now;S.Next=Now+.5;return;
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
        const auto& Profile=Client->GetProfile().GetValue();if(Profile.Inventory.Items.IsEmpty()){Fail(TEXT("Missing lifecycle inventory"));return;}
        const auto& Item=Profile.Inventory.Items[0];S.FavoriteItem=Item.InstanceId;S.FavoriteValue=!Item.bFavorite;
        FAetherPlayerCommand Command;Command.ProtocolVersion=AetherCommands::LatestProtocolVersion;Command.Type=EAetherCommandType::SetItemFavorite;
        Command.ExpectedProfileRevision=Profile.Revision;Command.CommandId=AetherTransactions::NewCommandId(Profile.Revision);Command.ItemInstanceId=Item.InstanceId;Command.Enabled=S.FavoriteValue;
        TArray<uint8> Bytes;FString Why;
        if(!AetherCommands::Encode(Command,Bytes,Why)||!Client->Submit(Client->GetChannel(),Client->GetOwnerIdentity(),Bytes,Why)){Fail(Why);return;}
        S.Step=3;S.OperationAt=Now;S.Next=Now+.1;return;
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
