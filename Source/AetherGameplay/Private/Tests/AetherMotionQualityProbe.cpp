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
  double MinPelvis=DBL_MAX,MaxPelvis=-DBL_MAX,MaxRoot=0,MaxFoot=0,MinFoot=DBL_MAX,FootAt=0;
  FVector PreviousFeet[2];bool PreviousContact[2]={false,false};TArray<double> ContactSpeeds;
  float MaxFootWeight=0;
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
 const bool EquipmentFixture=FParse::Param(FCommandLine::Get(),TEXT("AetherQualityEquipment"));
 static const TCHAR* GearNames[]={TEXT("SwordIdle"),TEXT("SwordWalk"),TEXT("ShieldIdle"),TEXT("ShieldWalk"),TEXT("HammerIdle"),TEXT("HammerWalk"),TEXT("StaffIdle"),TEXT("StaffWalk")};
 const auto PhaseName=[&](int32 Index){return EquipmentFixture?GearNames[Index%8]:Styles[Index%8];};
 const auto BeginPhase=[&](int32 Index)
 {
  S.Phase=Index;S.PhaseAt=Now;S.StableAt=0;S.Captures=0;S.ChangedFrames=0;S.Frames=0;S.Previous.Reset();S.Times.Reset();S.Sequences.Reset();
  S.MinPelvis=DBL_MAX;S.MaxPelvis=-DBL_MAX;S.MaxRoot=0;S.MaxFoot=0;S.MinFoot=DBL_MAX;S.FootAt=0;S.ContactSpeeds.Reset();S.PreviousContact[0]=S.PreviousContact[1]=false;S.MaxFootWeight=0;
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
  if(EquipmentFixture){
   // 仅渲染资源夹具恢复公开 Loadout；不写库存/任务，不把这一检查作为装备事务通过。
   TArray<FAetherEquippedSlot> Slots;
   const auto Add=[&](FName Slot,FName Item){FAetherEquippedSlot Equipped;Equipped.Slot=Slot;Equipped.ItemId=Item;Slots.Add(Equipped);};
   const int32 Kind=(Index%8)/2;
   Add(TEXT("MainHand"),Kind==2?TEXT("TrainingHammer"):Kind==3?TEXT("TideStaff"):TEXT("IronSword"));
   if(Kind==1)Add(TEXT("OffHand"),TEXT("IronShield"));
   const bool Iron=(Kind%2)!=0;
   Add(TEXT("Head"),Iron?TEXT("IronHelm"):TEXT("LeatherCap"));Add(TEXT("Chest"),Iron?TEXT("IronCuirass"):TEXT("LeatherVest"));
   Add(TEXT("Hands"),Iron?TEXT("IronGauntlets"):TEXT("LeatherGloves"));Add(TEXT("Legs"),Iron?TEXT("IronGreaves"):TEXT("LeatherLeggings"));
   Add(TEXT("Feet"),Iron?TEXT("IronBoots"):TEXT("LeatherBoots"));Add(TEXT("Neck"),Iron?TEXT("SilverNecklace"):TEXT("CopperNecklace"));
   Add(TEXT("Ring1"),TEXT("CopperRing"));Add(TEXT("Ring2"),TEXT("SilverRing"));
   if(!C->Equipment->RestoreLoadout(Slots)){Fail(TEXT("Authored equipment fixture rejected"));return;}
  }
  Motion->InvalidateMotion();
  S.Row=MakeShared<FJsonObject>();S.Row->SetStringField(TEXT("body"),Index<8?TEXT("Manny"):TEXT("Quinn"));
  S.Row->SetStringField(TEXT("style"),PhaseName(Index));
  UE_LOG(LogTemp,Display,TEXT("V10_MOTION_QUALITY_BEGIN body=%s style=%s"),Index<8?TEXT("Manny"):TEXT("Quinn"),PhaseName(Index));
 };
 if(S.Phase<0)BeginPhase(0);if(S.Done)return;
 const int32 Style=EquipmentFixture?S.Phase%2:S.Phase%8;
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
 for(int32 I=0;I<2;++I){
  const FVector Foot=Mesh->GetSocketLocation(I?TEXT("foot_r"):TEXT("foot_l"));const double Height=Foot.Z-Ground;
  S.MinFoot=FMath::Min(S.MinFoot,Height);S.MaxFoot=FMath::Max(S.MaxFoot,FMath::Abs(Height));
  const bool Contact=Height<=14;
  if(Contact&&S.PreviousContact[I]&&S.FootAt>0&&Now>S.FootAt)
   S.ContactSpeeds.Add(FVector::Dist2D(Foot,S.PreviousFeet[I])/(Now-S.FootAt));
  S.PreviousFeet[I]=Foot;S.PreviousContact[I]=Contact;
 }
 S.FootAt=Now;
 if(auto* Anim=Cast<UAetherAnimInstance>(Mesh->GetAnimInstance()))S.MaxFootWeight=FMath::Max(S.MaxFootWeight,Anim->FootWeight);
 if(!S.Sequences.Contains(Clip->Stamp.RequestSequence)){S.Sequences.Add(Clip->Stamp.RequestSequence);S.Times.Add(Clip->InferenceSeconds*1000);}
 if(S.Captures<2&&Now-S.StableAt>S.Captures*.5)
 {
  const FString Name=FString::Printf(TEXT("%02d-%s-%s-%d.png"),S.Phase,S.Phase<8?TEXT("Manny"):TEXT("Quinn"),PhaseName(S.Phase),S.Captures);
  FScreenshotRequest::RequestScreenshot(S.Dir/Name,true,false);++S.Captures;
 }
 if(Now-S.StableAt<3||S.Sequences.Num()<2)return;
 if(S.ChangedFrames<5){Fail(TEXT("Final authored body graph is frozen"));return;}
 if(EquipmentFixture){
  const auto* Anim=Cast<UAetherAnimInstance>(Mesh->GetAnimInstance());const auto* Main=C->Equipment->InSlot(TEXT("MainHand"));
  if(!Anim||!Main||Anim->GripWeights[1]<.95||!C->Equipment->VisualForSlot(TEXT("MainHand"))){Fail(TEXT("Weapon hand layer/visual absent"));return;}
  int32 Curled=0;const auto& Ref=Mesh->GetSkeletalMeshAsset()->GetRefSkeleton();
  for(const TCHAR* Finger:{TEXT("index_02_r"),TEXT("middle_02_r"),TEXT("ring_02_r"),TEXT("pinky_02_r")}){
   const int32 Index=Mesh->GetBoneIndex(Finger);
   if(Index!=INDEX_NONE&&Pose.IsValidIndex(Index)&&!Pose[Index].GetRotation().Equals(Ref.GetRefBonePose()[Index].GetRotation(),.03f))++Curled;
  }
  if(Curled<3){Fail(TEXT("Official grip pose did not close the fingers"));return;}
  S.Row->SetNumberField(TEXT("curledFingers"),Curled);
  if(Main->bOccupiesBothHands){
   const FVector Grip=Mesh->GetSocketTransform(TEXT("hand_r")).TransformPosition(Main->SupportHandOffset);
   const double Error=FVector::Dist(Grip,Mesh->GetSocketLocation(TEXT("hand_l")));
   S.Row->SetNumberField(TEXT("supportGripErrorCm"),Error);
   if(Error>8){Fail(FString::Printf(TEXT("Support hand misses real grip %.3f cm"),Error));return;}
  }
 }
 S.Row->SetNumberField(TEXT("frames"),S.Frames);S.Row->SetNumberField(TEXT("changedFrames"),S.ChangedFrames);
 S.Row->SetNumberField(TEXT("minPelvisCm"),S.MinPelvis);S.Row->SetNumberField(TEXT("maxPelvisCm"),S.MaxPelvis);
 S.Row->SetNumberField(TEXT("maxRootHeightCm"),S.MaxRoot);S.Row->SetNumberField(TEXT("maxFootHeightAboveGroundCm"),S.MaxFoot);
 S.Row->SetNumberField(TEXT("maxFootIKWeight"),S.MaxFootWeight);
 S.Row->SetNumberField(TEXT("minFootHeightAboveGroundCm"),S.MinFoot);
 S.ContactSpeeds.Sort();S.Row->SetNumberField(TEXT("contactSpeedSamples"),S.ContactSpeeds.Num());
 if(!S.ContactSpeeds.IsEmpty())S.Row->SetNumberField(TEXT("p95ContactSpeedCmPerSecond"),S.ContactSpeeds[FMath::Clamp(FMath::CeilToInt(S.ContactSpeeds.Num()*.95)-1,0,S.ContactSpeeds.Num()-1)]);
 S.Row->SetStringField(TEXT("actor"),C->GetActorLocation().ToString());S.Row->SetStringField(TEXT("mesh"),Mesh->GetComponentLocation().ToString());
 S.Row->SetNumberField(TEXT("floorZ"),Ground);S.Row->SetBoolField(TEXT("recentlyRendered"),Mesh->WasRecentlyRendered(.2f));
 TArray<TSharedPtr<FJsonValue>> Times;for(double Time:S.Times)Times.Add(MakeShared<FJsonValueNumber>(Time));S.Row->SetArrayField(TEXT("inferenceMs"),Times);
 S.Rows.Add(MakeShared<FJsonValueObject>(S.Row));
 {auto Progress=MakeShared<FJsonObject>();Progress->SetArrayField(TEXT("completedStyles"),S.Rows);Progress->SetBoolField(TEXT("complete"),false);
  FString Json;FJsonSerializer::Serialize(Progress,TJsonWriterFactory<>::Create(&Json));FFileHelper::SaveStringToFile(Json,*(S.Dir/TEXT("progress.json")));}
 if(S.Phase<15){BeginPhase(S.Phase+1);return;}
 auto Report=MakeShared<FJsonObject>();Report->SetNumberField(TEXT("backend"),S.Backend);Report->SetArrayField(TEXT("styles"),S.Rows);
 Report->SetBoolField(TEXT("graphChecksPassed"),true);Report->SetBoolField(TEXT("visualQualityApproved"),false);
 Report->SetStringField(TEXT("scope"),TEXT("Isolated native style/retarget fixture using actual animation graph; gameplay tick paused. Review images/contact metrics separately."));
 FString Json;FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Json));
 if(!FFileHelper::SaveStringToFile(Json,*(S.Dir/TEXT("result.json")))){Fail(TEXT("Report write failed"));return;}
 S.Done=true;UE_LOG(LogTemp,Display,TEXT("V10_MOTION_QUALITY_GRAPH_PASS styles=16"));FPlatformMisc::RequestExit(false);
#endif
}
