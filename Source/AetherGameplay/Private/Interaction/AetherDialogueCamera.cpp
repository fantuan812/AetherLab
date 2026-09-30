#include "Interaction/AetherDialogueSession.h"
#include "Interaction/AetherDialogueCameraActor.h"
#include "Characters/AetherFrontierCharacter.h"
#include "World/AetherFrontierProp.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "CollisionShape.h"

bool UAetherDialogueSession::CameraPosition(FVector& Position,FRotator& Rotation) const
{
    const auto* T=Target.Get();if(!T||!Shot.IsSet())return false;
    const auto& D=Shot.GetValue();const auto Transform=T->GetActorTransform();
    const FVector Look=Transform.TransformPositionNoScale(D.LookAtOffset);
    Position=Transform.TransformPositionNoScale(D.Offset);
    FCollisionQueryParams Q(SCENE_QUERY_STAT(DialogueCamera),false,T);Q.AddIgnoredActor(Player.Get());Q.AddIgnoredActor(LocalCamera.Get());
    FHitResult Hit;
    if(GetWorld()->SweepSingleByChannel(Hit,Look,Position,FQuat::Identity,ECC_Camera,FCollisionShape::MakeSphere(float(D.ProbeRadiusCm)),Q))
    {
        if(Hit.bStartPenetrating)return false;
        Position=Hit.Location;
    }
    if(FVector::DistSquared(Position,Look)<=FMath::Square(D.ProbeRadiusCm))return false;
    Rotation=(Look-Position).Rotation();return true;
}
AActor* UAetherDialogueSession::RestoreTarget() const
{
    auto* PC=CameraController.Get();if(!PC)return nullptr;
    auto* Previous=PreviousViewTarget.Get();
    if(Previous&&!Previous->IsActorBeingDestroyed()&&(Previous!=CameraPawn.Get()||PC->GetPawn()==CameraPawn.Get()))return Previous;
    // Pawn replacement invalidates the old controlled viewpoint. This is view recovery, never a saved-data conversion.
    auto* Pawn=PC->GetPawn();return Pawn&&!Pawn->IsActorBeingDestroyed()?static_cast<AActor*>(Pawn):static_cast<AActor*>(PC);
}
void UAetherDialogueSession::DestroyCamera()
{
    if(auto* Camera=LocalCamera.Get())Camera->Destroy();
    LocalCamera.Reset();CameraController.Reset();CameraPawn.Reset();PreviousViewTarget.Reset();Shot.Reset();bReturningCamera=false;CameraElapsed=0;
}
void UAetherDialogueSession::SurrenderCamera()
{
    if(auto* Camera=Cast<AAetherDialogueCameraActor>(LocalCamera.Get()))
    {
        auto* PC=CameraController.Get();Camera->RetireWhenUnreferenced(PC?PC->PlayerCameraManager:nullptr);
    }
    // Do not destroy the outgoing view and do not restore anything. The actor owns deferred cleanup even after Deinitialize.
    LocalCamera.Reset();CameraController.Reset();CameraPawn.Reset();PreviousViewTarget.Reset();Shot.Reset();bReturningCamera=false;CameraElapsed=0;
}
bool UAetherDialogueSession::BeginCamera(const FAetherDialoguePresentation& Presentation,FString& Reason)
{
    ReleaseCamera(true);if(!Presentation.Camera.IsSet())return true;
    auto* PC=OwningController.Get();
    if(!PC||!PC->IsLocalController()||PC->GetLocalPlayer()!=GetLocalPlayer()||!PC->PlayerCameraManager)
    {Reason=TEXT("本地镜头拥有者不可用。");return false;}
    if(PC->PlayerCameraManager->PendingViewTarget.Target)
    {Reason=TEXT("另一个镜头切换尚未完成。");return false;}
    Shot=Presentation.Camera;FVector Desired;FRotator Rotation;
    if(!CameraPosition(Desired,Rotation)){Shot.Reset();Reason=TEXT("对话镜头位置被遮挡。");return false;}
    CameraController=PC;CameraPawn=PC->GetPawn();PreviousViewTarget=PC->GetViewTarget();
    PC->GetPlayerViewPoint(CameraFromLocation,CameraFromRotation);CameraFromFov=PC->PlayerCameraManager->GetFOVAngle();
    FActorSpawnParameters Params;Params.ObjectFlags|=RF_Transient;Params.Owner=PC;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Camera=GetWorld()->SpawnActor<AAetherDialogueCameraActor>(CameraFromLocation,CameraFromRotation,Params);
    if(!Camera){DestroyCamera();Reason=TEXT("本地对话镜头无法创建。");return false;}
    Camera->SetReplicates(false);Camera->SetActorEnableCollision(false);Camera->GetCameraComponent()->SetFieldOfView(CameraFromFov);
    LocalCamera=Camera;CameraElapsed=0;bReturningCamera=false;PC->SetViewTarget(Camera);
    return true;
}
void UAetherDialogueSession::ReleaseCamera(bool Immediate)
{
    auto* Camera=LocalCamera.Get();auto* PC=CameraController.Get();
    if(!Camera){DestroyCamera();return;}
    // Never overwrite a later camera owner, even on interruption/deinitialization.
    // This session never creates an engine pending blend. Any pending target belongs to an external owner.
    if(PC&&PC->PlayerCameraManager&&PC->PlayerCameraManager->PendingViewTarget.Target){SurrenderCamera();return;}
    if(!PC||PC->IsActorBeingDestroyed()||!PC->IsLocalController()||PC->GetLocalPlayer()!=GetLocalPlayer()||PC->GetViewTarget()!=Camera){DestroyCamera();return;}
    if(Immediate||!Shot.IsSet()||Shot->BlendOutSeconds<=0)
    {if(auto* Restore=RestoreTarget())PC->SetViewTarget(Restore);DestroyCamera();return;}
    if(bReturningCamera)return;
    bReturningCamera=true;CameraElapsed=0;CameraFromLocation=Camera->GetActorLocation();CameraFromRotation=Camera->GetActorRotation();CameraFromFov=Camera->GetCameraComponent()->FieldOfView;
}
void UAetherDialogueSession::TickCamera(float Dt)
{
    auto* Camera=LocalCamera.Get();if(!Camera)return;
    auto* PC=CameraController.Get();
    if(!PC||PC->IsActorBeingDestroyed()||PC->GetLocalPlayer()!=GetLocalPlayer()||GetLocalPlayer()->GetPlayerController(GetWorld())!=PC)
    {ReleaseCamera(true);return;}
    if(PC->PlayerCameraManager&&PC->PlayerCameraManager->PendingViewTarget.Target)
    {SurrenderCamera();if(View.IsSet())Close();return;}
    if(PC->GetViewTarget()!=Camera)
    {SurrenderCamera();if(View.IsSet())Close();return;}
    if(!Shot.IsSet()){ReleaseCamera(true);return;}
    FVector Position;FRotator Rotation;float Fov=0;double Duration=0;
    if(bReturningCamera)
    {
        auto* Restore=RestoreTarget();if(!Restore){ReleaseCamera(true);return;}
        FMinimalViewInfo POV;Restore->CalcCamera(Dt,POV);Position=POV.Location;Rotation=POV.Rotation;Fov=POV.FOV;Duration=Shot->BlendOutSeconds;
    }
    else
    {
        if(!CameraPosition(Position,Rotation)){Close();return;}
        Fov=float(Shot->Fov);Duration=Shot->BlendInSeconds;
    }
    if(FMath::IsFinite(Dt)&&Dt>0)CameraElapsed+=Dt;
    const float Alpha=Duration<=0?1.f:float(FMath::Clamp(CameraElapsed/Duration,0.,1.));
    FVector Blended=FMath::Lerp(CameraFromLocation,Position,Alpha);
    if(!bReturningCamera)
    {
        // Check the actual interpolated path too, not merely the authored final shot.
        FCollisionQueryParams Q(SCENE_QUERY_STAT(DialogueCameraBlend),false,Camera);Q.AddIgnoredActor(Player.Get());Q.AddIgnoredActor(Target.Get());
        FHitResult Hit;
        if(GetWorld()->SweepSingleByChannel(Hit,Camera->GetActorLocation(),Blended,FQuat::Identity,ECC_Camera,FCollisionShape::MakeSphere(float(Shot->ProbeRadiusCm)),Q))
        {if(Hit.bStartPenetrating){Close();return;}Blended=Hit.Location;}
    }
    Camera->SetActorLocationAndRotation(Blended,FQuat::Slerp(CameraFromRotation.Quaternion(),Rotation.Quaternion(),Alpha));
    Camera->GetCameraComponent()->SetFieldOfView(FMath::Lerp(CameraFromFov,Fov,Alpha));
    if(bReturningCamera&&Alpha>=1)ReleaseCamera(true);
}
