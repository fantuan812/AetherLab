#include "Diagnostics/AetherPackageCapture.h"
#include "UI/AetherFrontierHUD.h"
#include "AetherFrontierPanel.h"
#include "Characters/AetherFrontierCharacter.h"
#include "Networking/AetherCommandClient.h"
#include "Presentation/AetherMenuSubsystem.h"
#include "Preview/AetherCharacterPreviewSubsystem.h"
#include "AetherMotionComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "MotionBricksScheduler.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Engine.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "UnrealClient.h"
#include "HAL/PlatformTime.h"
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

namespace
{
int32 PosedBones(USkeletalMeshComponent* Mesh)
{
 if(!Mesh||!Mesh->GetSkeletalMeshAsset()||!Mesh->GetAnimInstance())return 0;
 const auto& Ref=Mesh->GetSkeletalMeshAsset()->GetRefSkeleton().GetRefBonePose();
 const auto& Actual=Mesh->GetBoneSpaceTransforms();int32 Count=0;
 for(int32 I=1;I<FMath::Min(Ref.Num(),Actual.Num());++I)
 {
  if(Actual[I].ContainsNaN())return 0;
  if(!Actual[I].GetRotation().Equals(Ref[I].GetRotation(),.01f)||!Actual[I].GetTranslation().Equals(Ref[I].GetTranslation(),.5f))++Count;
 }
 return Count;
}
}
void AetherPackageCapture::Tick(AAetherFrontierHUD* HUD,UAetherFrontierPanel* Panel)
{
    // Shipping 仍保留只读支持诊断：只经正式菜单服务开页、截图和记录资源，不授予物品、不改玩家进度。
    // 必须同时给出独立 UserDir 和隔离前缀；正常启动没有诊断 Tick 工作。
    static FString Token=[](){FString Value;FParse::Value(FCommandLine::Get(),TEXT("AetherPackageCapture="),Value);return Value;}();
    if(Token.IsEmpty()||!HUD||!Panel)return;
    struct FState{double Start=FPlatformTime::Seconds(),Next=0,PoseReadyAt=0;int32 Step=0,Backend=0,BodyPosed=0,SourcePosed=0;float GeneratedWeight=0;double RootHeight=0,PelvisHeight=0;bool Done=false;FString Dir;TMap<FString,FString> PoseState;};
    static FState S;if(S.Done)return;
    if(S.Dir.IsEmpty())
    {
        FGuid Id;FString Prefix,UserDir;
        FParse::Value(FCommandLine::Get(),TEXT("AetherSavePrefix="),Prefix);
        FParse::Value(FCommandLine::Get(),TEXT("UserDir="),UserDir);
        if(!FGuid::ParseExact(Token,EGuidFormats::Digits,Id)||UserDir.IsEmpty()||!Prefix.StartsWith(TEXT("V10Package_")))
        {S.Done=true;FPlatformMisc::RequestExitWithStatus(false,2);return;}
        S.Dir=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("PackageCapture")/Token);
        IFileManager::Get().MakeDirectory(*S.Dir,true);
        FParse::Value(FCommandLine::Get(),TEXT("AetherPackageBackend="),S.Backend);S.Backend=FMath::Clamp(S.Backend,0,2);
        if(auto* Backend=IConsoleManager::Get().FindConsoleVariable(TEXT("aether.Motion.Backend")))Backend->Set(S.Backend,ECVF_SetByCode);
    }
    const auto Finish=[&](bool Passed,const FString& Reason)
    {
        auto Result=MakeShared<FJsonObject>();Result->SetBoolField(TEXT("passed"),Passed);Result->SetStringField(TEXT("reason"),Reason);
        Result->SetBoolField(TEXT("shipping"),UE_BUILD_SHIPPING!=0);Result->SetBoolField(TEXT("fullGameplayAcceptance"),false);
        Result->SetNumberField(TEXT("bodyPosedBones"),S.BodyPosed);Result->SetNumberField(TEXT("sourcePosedBones"),S.SourcePosed);
        Result->SetNumberField(TEXT("generatedWeight"),S.GeneratedWeight);
        Result->SetNumberField(TEXT("rootHeightCm"),S.RootHeight);Result->SetNumberField(TEXT("pelvisHeightCm"),S.PelvisHeight);
        auto Pose=MakeShared<FJsonObject>();for(const auto& V:S.PoseState)Pose->SetStringField(V.Key,V.Value);Result->SetObjectField(TEXT("poseState"),Pose);
        Result->SetNumberField(TEXT("backend"),S.Backend);Result->SetNumberField(TEXT("step"),S.Step);
        Result->SetStringField(TEXT("projectDir"),FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()));
        Result->SetNumberField(TEXT("nativeCalls"),double(AetherMotionScheduler().Inspect().NativeCalls));
        if(auto* C=Cast<AAetherFrontierCharacter>(HUD->GetOwningPawn());C&&C->Motion)Result->SetStringField(TEXT("motion"),C->Motion->Status());
        FString Json;FJsonSerializer::Serialize(Result,TJsonWriterFactory<>::Create(&Json));
        const bool Saved=FFileHelper::SaveStringToFile(Json,*(S.Dir/TEXT("result.json")));
        S.Done=true;FPlatformMisc::RequestExitWithStatus(false,Passed&&Saved?0:1);
    };
    const double Now=FPlatformTime::Seconds();
    if(Now-S.Start>150){Finish(false,TEXT("Native/menu/preview startup deadline"));return;}
    if(Now<S.Next)return;
    auto* PC=HUD->GetOwningPlayerController();auto* LP=PC?PC->GetLocalPlayer():nullptr;
    auto* C=Cast<AAetherFrontierCharacter>(HUD->GetOwningPawn());if(!LP||!C)return;
    auto* Client=LP->GetSubsystem<UAetherCommandClient>();auto* Menu=LP->GetSubsystem<UAetherMenuSubsystem>();auto* Preview=LP->GetSubsystem<UAetherCharacterPreviewSubsystem>();
    if(!C->Ready()||C->bTravelPending||!Client->GetProfile().IsSet()||!Client->GetChannel().IsValid())return;
    if(S.Step==0)
    {
        S.BodyPosed=PosedBones(C->GetMesh());S.SourcePosed=PosedBones(C->Motion?C->Motion->GetSourceMesh():nullptr);
        S.GeneratedWeight=C->Motion?C->Motion->GeneratedWeight():0;
        // 调用模型成功仍可能没有驱动最终图；实际源姿态和人物姿态均须离开参考姿势。
        S.RootHeight=C->GetMesh()->GetSocketTransform(TEXT("root"),RTS_Component).GetLocation().Z;
        S.PelvisHeight=C->GetMesh()->GetSocketTransform(TEXT("pelvis"),RTS_Component).GetLocation().Z;
        const bool Ready=FMath::Abs(S.RootHeight)<10&&S.PelvisHeight>20&&S.PelvisHeight<180&&S.BodyPosed>=8&&(S.Backend==0||(AetherMotionScheduler().Inspect().NativeCalls>=2&&S.SourcePosed>=8&&S.GeneratedWeight>.9f));
        if(!Ready){S.PoseReadyAt=0;return;}
        if(S.PoseReadyAt==0){S.PoseReadyAt=Now;return;}
        if(Now-S.PoseReadyAt<2)return;
        S.PoseState.Add(TEXT("actor"),C->GetActorLocation().ToString());S.PoseState.Add(TEXT("mesh"),C->GetMesh()->GetComponentTransform().ToString());
        S.PoseState.Add(TEXT("camera"),PC->PlayerCameraManager->GetCameraLocation().ToString());
        S.PoseState.Add(TEXT("cameraRotation"),PC->PlayerCameraManager->GetCameraRotation().ToString());
        for(const TCHAR* Bone:{TEXT("root"),TEXT("pelvis"),TEXT("head"),TEXT("foot_l"),TEXT("foot_r")})
        {
            S.PoseState.Add(Bone,C->GetMesh()->GetSocketTransform(Bone,RTS_Component).ToString());
            FVector2D Screen;const bool Visible=PC->ProjectWorldLocationToScreen(C->GetMesh()->GetSocketLocation(Bone),Screen);
            S.PoseState.Add(FString(Bone)+TEXT("Screen"),FString::Printf(TEXT("%d %s"),Visible,*Screen.ToString()));
        }
        FScreenshotRequest::RequestScreenshot(S.Dir/TEXT("World.png"),true,false);
        S.Step=1;S.Next=Now+1;return;
    }
    static const TCHAR* Names[]={TEXT("Inventory"),TEXT("Journal"),TEXT("Skills"),TEXT("Map"),TEXT("Party"),TEXT("Settings")};
    if(S.Step<=6)
    {
        if(Menu->GetPage()!=EAetherMenuPage(S.Step)){Menu->OpenPage(EAetherMenuPage(S.Step));S.Next=Now+3;return;}
        if(!Panel->IsActivated()||!Panel->GetClass()->GetPathName().StartsWith(TEXT("/Game/UI/Widgets/WBP_PlayerMenu")))
        {Finish(false,TEXT("Formal menu widget missing"));return;}
        if(S.Step==1&&(!Preview->IsPreviewActive()||!Preview->GetRenderTarget()||!Preview->GetDisplayMaterial()))return;
        FScreenshotRequest::RequestScreenshot(S.Dir/(FString(Names[S.Step-1])+TEXT(".png")),true,false);
        // 截图在帧末完成；下一次 Tick 才切页，避免拿相邻页面冒充当前页。
        ++S.Step;S.Next=Now+1;return;
    }
    if(S.Step==7){Menu->Close();S.Step=8;S.Next=Now+1;return;}
    if(Menu->IsOpen()||Preview->GetRenderTarget()||PC->bShowMouseCursor){Finish(false,TEXT("Menu close did not release preview/input"));return;}
    Finish(true,TEXT("Native startup, formal menu assets, transparent preview resource and menu close completed; inspect screenshots separately."));
}
