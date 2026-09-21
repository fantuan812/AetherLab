#include "Tests/AetherMotionQualityProbe.h"
#if !UE_BUILD_SHIPPING
#include "Framework/AetherPlayerController.h"
#include "Characters/AetherFrontierCharacter.h"
#include "Animation/AetherAnimation.h"
#include "Movement/AetherCharacterMovement.h"
#include "AetherMotionComponent.h"
#include "MotionBricksScheduler.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/PlatformTime.h"
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "UnrealClient.h"
#endif
void AetherMotionQualityProbe::Tick(AAetherPlayerController* PC,float Dt)
{
#if !UE_BUILD_SHIPPING
 if(!FParse::Param(FCommandLine::Get(),TEXT("AetherMotionQuality"))||!PC||!PC->IsLocalController()||!PC->HasAuthority())return;
 struct FState
 {
  double Started=FPlatformTime::Seconds(),PhaseAt=0,StableAt=0;int32 Phase=-1,Captures=0,Backend=1;
  bool Done=false,Staged=false;FVector Origin;FString Dir;TArray<TSharedPtr<FJsonValue>> Rows;TSharedPtr<FJsonObject> Row;
  TSet<uint64> Sequences;TArray<double> Times;TArray<FTransform> Previous;int32 ChangedFrames=0,Frames=0;
  double MinPelvis=DBL_MAX,MaxPelvis=-DBL_MAX,MaxRoot=0,MaxFoot=0;float MaxFootWeight=0;
 };
 static FState S;if(S.Done)return;const double Now=FPlatformTime::Seconds();
 const auto Fail=[&](const FString& Why){S.Done=true;UE_LOG(LogTemp,Error,TEXT("V10_MOTION_QUALITY_FAIL phase=%d %s"),S.Phase,*Why);FPlatformMisc::RequestExitWithStatus(false,1);};
 if(S.Dir.IsEmpty())
 {
  FString Prefix;FParse::Value(FCommandLine::Get(),TEXT("AetherSavePrefix="),Prefix);
  FParse::Value(FCommandLine::Get(),TEXT("AetherQualityReport="),S.Dir);
  if(!Prefix.StartsWith(TEXT("V10Quality_"))||S.Dir.IsEmpty()){Fail(TEXT("Isolated diagnostic paths required"));return;}
  IFileManager::Get().MakeDirectory(*S.Dir,true);
  FParse::Value(FCommandLine::Get(),TEXT("AetherQualityBackend="),S.Backend);
  if(S.Backend<1||S.Backend>2){Fail(TEXT("Real native backend required"));return;}
  IConsoleManager::Get().FindConsoleVariable(TEXT("aether.Motion.Backend"))->Set(S.Backend,ECVF_SetByCode);
 }
 auto* C=Cast<AAetherFrontierCharacter>(PC->GetPawn());
 if(!C||!C->Ready()||C->bTravelPending||!C->GetMesh()->GetSkeletalMeshAsset()){if(Now-S.Started>90)Fail(TEXT("Pawn readiness deadline"));return;}
 if(!S.Staged){S.Staged=true;C->BeginSafeTravel(FVector(-6500,-22000,120));return;}
 auto* Move=C->GetCharacterMovement();auto* Mesh=C->GetMesh();auto* Motion=C->Motion.Get();
 static const TCHAR* Styles[]={TEXT("Idle"),TEXT("Walk"),TEXT("Combat"),TEXT("StrafeLeft"),TEXT("StrafeRight"),TEXT("Injured"),TEXT("Crouch"),TEXT("CrouchIdle")};
 const auto BeginPhase=[&](int32 Index)
 {
  S.Phase=Index;S.PhaseAt=Now;S.StableAt=0;S.Captures=0;S.ChangedFrames=0;S.Frames=0;S.Previous.Reset();S.Times.Reset();S.Sequences.Reset();
  S.MinPelvis=DBL_MAX;S.MaxPelvis=-DBL_MAX;S.MaxRoot=0;S.MaxFoot=0;S.MaxFootWeight=0;
  // 独立动作质量夹具暂停玩法意图写入；仍运行真实 Movement、MotionComponent、AnimBP 和渲染。
  // 不授予主线证据，不把此夹具记作完整游玩或战斗验收。
  C->SetActorTickEnabled(false);
  if(auto* Locomotion=Cast<UAetherCharacterMovement>(Move)){Locomotion->WalkSpeed=120;Locomotion->CrouchSpeed=120;}
  Move->StopMovementImmediately();C->UnCrouch();
  if(Index==0)S.Origin=C->GetActorLocation();else C->SetActorLocation(S.Origin,false,nullptr,ETeleportType::TeleportPhysics);
  if(Index==8)
  {
   auto* Quinn=LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple.SKM_Quinn_Simple"));
   if(!Quinn){Fail(TEXT("Quinn asset missing"));return;}Mesh->SetSkeletalMeshAsset(Quinn);Mesh->InitAnim(true);
  }
  Motion->InvalidateMotion();
  S.Row=MakeShared<FJsonObject>();S.Row->SetStringField(TEXT("body"),Index<8?TEXT("Manny"):TEXT("Quinn"));
  S.Row->SetStringField(TEXT("style"),Styles[Index%8]);
  UE_LOG(LogTemp,Display,TEXT("V10_MOTION_QUALITY_BEGIN body=%s style=%s"),Index<8?TEXT("Manny"):TEXT("Quinn"),Styles[Index%8]);
 };
 if(S.Phase<0)BeginPhase(0);if(S.Done)return;
 const int32 Style=S.Phase%8;
 if(Style>=6)C->Crouch();else C->UnCrouch();
 const FVector Direction=Style==3?-C->GetActorRightVector():Style==4?C->GetActorRightVector():C->GetActorForwardVector();
 if(Style!=0&&Style!=7)C->AddMovementInput(Direction,1);
 Motion->SetIntent(true,Styles[Style],FGuid(0,0,0,991));
 auto Clip=Motion->PoseClip();auto* Source=Motion->GetSourceMesh();
 if(Now-S.PhaseAt>60){Fail(TEXT("Style failed to produce a stable actual pose: ")+Motion->Status());return;}
 if(!Source||!Clip||Motion->GeneratedWeight()<.95f||Now-S.PhaseAt<3){S.StableAt=0;return;}
 // 相对偏移和世界缓存都须匹配实际胶囊，防止父类蹲起回调再次把 Mesh 提离地面。
 if(FMath::Abs(Mesh->GetRelativeLocation().Z+C->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight())>.25||
    FMath::Abs(Mesh->GetComponentLocation().Z-C->GetActorLocation().Z+C->GetCapsuleComponent()->GetScaledCapsuleHalfHeight())>.25){Fail(TEXT("Body mesh/capsule ground offset diverged"));return;}
 const double Root=Mesh->GetSocketTransform(TEXT("root"),RTS_Component).GetLocation().Z;
 const double Pelvis=Mesh->GetSocketTransform(TEXT("pelvis"),RTS_Component).GetLocation().Z;
 if(!FMath::IsFinite(Pelvis)||FMath::Abs(Root)>10||Pelvis<15||Pelvis>180){
 auto* A=Cast<UAetherAnimInstance>(Mesh->GetAnimInstance());
 Fail(FString::Printf(TEXT("Retarget root=%.3f pelvis=%.3f source=%.3f generated=%.3f controlled=%.3f crouched=%d"),Root,Pelvis,Source->GetSocketTransform(TEXT("pelvis_skel"),RTS_Component).GetLocation().Z,Motion->GeneratedWeight(),A?A->ControlledWeight:-1,C->bIsCrouched));return;
 }
 if(S.StableAt==0)S.StableAt=Now;
 ++S.Frames;S.MinPelvis=FMath::Min(S.MinPelvis,Pelvis);S.MaxPelvis=FMath::Max(S.MaxPelvis,Pelvis);S.MaxRoot=FMath::Max(S.MaxRoot,FMath::Abs(Root));
 const auto Pose=Mesh->GetBoneSpaceTransforms();int32 Changed=0;
 for(int32 I=1;I<Pose.Num();++I){if(Pose[I].ContainsNaN()){Fail(TEXT("Invalid bone transform"));return;}if(S.Previous.IsValidIndex(I)&&!Pose[I].GetRotation().Equals(S.Previous[I].GetRotation(),.0001f))++Changed;}
 if(Changed>=3)++S.ChangedFrames;S.Previous=Pose;
 const double Ground=Move->CurrentFloor.HitResult.ImpactPoint.Z;
 for(const TCHAR* Foot:{TEXT("foot_l"),TEXT("foot_r")})S.MaxFoot=FMath::Max(S.MaxFoot,FMath::Abs(Mesh->GetSocketLocation(Foot).Z-Ground));
 if(auto* Anim=Cast<UAetherAnimInstance>(Mesh->GetAnimInstance()))S.MaxFootWeight=FMath::Max(S.MaxFootWeight,Anim->FootWeight);
 if(!S.Sequences.Contains(Clip->Stamp.RequestSequence)){S.Sequences.Add(Clip->Stamp.RequestSequence);S.Times.Add(Clip->InferenceSeconds*1000);}
 if(S.Captures<2&&Now-S.StableAt>S.Captures*.5)
 {
  const FString Name=FString::Printf(TEXT("%02d-%s-%s-%d.png"),S.Phase,S.Phase<8?TEXT("Manny"):TEXT("Quinn"),Styles[Style],S.Captures);
  FScreenshotRequest::RequestScreenshot(S.Dir/Name,true,false);++S.Captures;
 }
 if(Now-S.StableAt<3||S.Sequences.Num()<2)return;
 if(S.ChangedFrames<5){Fail(TEXT("Final authored body graph is frozen"));return;}
 S.Row->SetNumberField(TEXT("frames"),S.Frames);S.Row->SetNumberField(TEXT("changedFrames"),S.ChangedFrames);
 S.Row->SetNumberField(TEXT("minPelvisCm"),S.MinPelvis);S.Row->SetNumberField(TEXT("maxPelvisCm"),S.MaxPelvis);
 S.Row->SetNumberField(TEXT("maxRootHeightCm"),S.MaxRoot);S.Row->SetNumberField(TEXT("maxFootHeightAboveGroundCm"),S.MaxFoot);
 S.Row->SetNumberField(TEXT("maxFootIKWeight"),S.MaxFootWeight);
 S.Row->SetStringField(TEXT("actor"),C->GetActorLocation().ToString());S.Row->SetStringField(TEXT("mesh"),Mesh->GetComponentLocation().ToString());
 S.Row->SetNumberField(TEXT("floorZ"),Ground);S.Row->SetBoolField(TEXT("recentlyRendered"),Mesh->WasRecentlyRendered(.2f));
 TArray<TSharedPtr<FJsonValue>> Times;for(double Time:S.Times)Times.Add(MakeShared<FJsonValueNumber>(Time));S.Row->SetArrayField(TEXT("inferenceMs"),Times);
 S.Rows.Add(MakeShared<FJsonValueObject>(S.Row));
 if(S.Phase<15){BeginPhase(S.Phase+1);return;}
 auto Report=MakeShared<FJsonObject>();Report->SetNumberField(TEXT("backend"),S.Backend);Report->SetArrayField(TEXT("styles"),S.Rows);
 Report->SetBoolField(TEXT("graphChecksPassed"),true);Report->SetBoolField(TEXT("visualQualityApproved"),false);
 Report->SetStringField(TEXT("scope"),TEXT("Isolated native style/retarget fixture using actual animation graph; gameplay tick paused. Review images/contact metrics separately."));
 FString Json;FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Json));
 if(!FFileHelper::SaveStringToFile(Json,*(S.Dir/TEXT("result.json")))){Fail(TEXT("Report write failed"));return;}
 S.Done=true;UE_LOG(LogTemp,Display,TEXT("V10_MOTION_QUALITY_GRAPH_PASS styles=16"));FPlatformMisc::RequestExit(false);
#endif
}
