#include "Preview/AetherCharacterPreviewActor.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/PointLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"

AAetherCharacterPreviewActor::AAetherCharacterPreviewActor()
{
    bReplicates=false;PrimaryActorTick.bCanEverTick=false;SetActorEnableCollision(false);
    auto* Root=CreateDefaultSubobject<USceneComponent>(TEXT("PreviewRoot"));SetRootComponent(Root);
    Body=CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("PreviewBody"));Body->SetupAttachment(Root);
    Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);Body->SetGenerateOverlapEvents(false);
    Body->SetCanEverAffectNavigation(false);Body->PrimaryComponentTick.bStartWithTickEnabled=false;
    Body->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    FallbackBody=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MissingBody"));FallbackBody->SetupAttachment(Root);
    FallbackBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);FallbackBody->SetGenerateOverlapEvents(false);
    FallbackBody->SetCanEverAffectNavigation(false);FallbackBody->SetVisibility(false);
    // 独立世界没有地图的天空环境；固定补光保证深色官方材质仍可辨识。
    auto* Fill=CreateDefaultSubobject<UPointLightComponent>(TEXT("PreviewFill"));Fill->SetupAttachment(Root);
    Fill->SetRelativeLocation(FVector(220,-100,180));Fill->SetIntensity(8000);Fill->SetAttenuationRadius(1200);Fill->SetCastShadows(false);
    Capture=CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("PreviewCapture"));Capture->SetupAttachment(Root);
    Capture->bCaptureEveryFrame=false;Capture->bCaptureOnMovement=false;
    Capture->PrimitiveRenderMode=ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
    Capture->FOVAngle=35.f;Capture->ShowFlags.SetFog(false);Capture->ShowFlags.SetAtmosphere(false);
    Capture->ShowFlags.SetMotionBlur(false);Capture->ShowFlags.SetTemporalAA(false);
}
FString AAetherCharacterPreviewActor::ApplyAppearance(USkeletalMesh* Mesh,FRotator Rotation,UAnimSequence* Idle,
    const TArray<FAetherEquipmentVisualSpec>& Equipment,UStaticMesh* Fallback)
{
    FString Warning;
    const bool NewBody=Body->GetSkeletalMeshAsset()!=Mesh;
    if(NewBody){Body->SetSkeletalMeshAsset(Mesh);Applied.Reset();}
    Body->SetRelativeRotation(Rotation);Body->SetVisibility(Mesh!=nullptr);
    FallbackBody->SetVisibility(!Mesh);
    if(!Mesh)
    {
        FallbackBody->SetStaticMesh(Fallback);FallbackBody->SetRelativeLocation(FVector(0,0,90));
        FallbackBody->SetRelativeScale3D(FVector(.6f,.4f,1.8f));Warning=TEXT("角色模型不可用，显示基础占位外形。");
    }
    // 参考姿态是缺失动画的明确兼容路径，不能把不兼容骨架动画强行播放到另一体型。
    if(Idle&&Mesh&&Idle->GetSkeleton()!=Mesh->GetSkeleton()){Idle=nullptr;Warning+=TEXT(" 待机动画与角色骨架不兼容。");}
    if(NewBody||AppliedIdle.Get()!=Idle)
    {
        Body->SetAnimationMode(EAnimationMode::AnimationSingleNode);
        if(Idle){Body->PlayAnimation(Idle,true);if(auto* Anim=Body->GetSingleNodeInstance())Anim->SetRootMotionMode(ERootMotionMode::IgnoreRootMotion);}
        else {Body->SetAnimation(nullptr);Body->Stop();}
        AppliedIdle=Idle;
    }
    if(Mesh&&!Idle)Warning+=TEXT(" 待机动画不可用，显示参考姿态。");
    TSet<FName> Desired;
    for(const auto& Spec:Equipment)
    {
        Desired.Add(Spec.Slot);
        auto* Loaded=Spec.Mesh.Get();const bool Missing=!Loaded; if(!Loaded)Loaded=Fallback;
        if(Missing)Warning+=FString::Printf(TEXT(" %s 装备网格不可用，显示占位网格。"),*Spec.Slot.ToString());
        auto* Existing=Visuals.FindRef(Spec.Slot).Get();const auto* Previous=Applied.Find(Spec.Slot);
        if(Existing&&Previous&&Previous->SameAppearance(Spec)&&Existing->GetStaticMesh()==Loaded)continue;
        if(!Existing){Existing=NewObject<UStaticMeshComponent>(this);AddInstanceComponent(Existing);Visuals.Add(Spec.Slot,Existing);}
        if(!Mesh||!AetherEquipmentVisuals::Attach(Body,Existing,Spec,Loaded))
        {
            RemoveInstanceComponent(Existing);Existing->DestroyComponent();Visuals.Remove(Spec.Slot);Applied.Remove(Spec.Slot);
            Warning+=FString::Printf(TEXT(" %s 缺少挂点 %s。"),*Spec.Slot.ToString(),*Spec.Socket.ToString());continue;
        }
        if(!Existing->IsRegistered())Existing->RegisterComponent();
        Applied.Add(Spec.Slot,Spec);
    }
    TArray<FName> Removed;for(const auto& Pair:Visuals)if(!Desired.Contains(Pair.Key))Removed.Add(Pair.Key);
    for(const auto Slot:Removed){RemoveInstanceComponent(Visuals[Slot]);Visuals[Slot]->DestroyComponent();Visuals.Remove(Slot);Applied.Remove(Slot);}
    // 白名单只含本预览 Actor 的组件；不可能捕获真实玩家、地图或其他本地玩家的舞台。
    Capture->ShowOnlyComponents.Empty();Capture->ShowOnlyComponent(Body);Capture->ShowOnlyComponent(FallbackBody);
    for(const auto& Pair:Visuals)Capture->ShowOnlyComponent(Pair.Value);
    return Warning.TrimStartAndEnd();
}
void AAetherCharacterPreviewActor::SetRenderTarget(UTextureRenderTarget2D* Target,bool Transparent)
{
    Capture->TextureTarget=Target;
    // SceneColor HDR 的 alpha 为反向不透明度，UI 材质负责 1-alpha；材质缺失时明确使用不透明兼容显示。
    Capture->CaptureSource=Transparent?ESceneCaptureSource::SCS_SceneColorHDR:ESceneCaptureSource::SCS_FinalColorLDR;
}
void AAetherCharacterPreviewActor::ClearAppearance()
{
    Capture->TextureTarget=nullptr;Capture->ShowOnlyComponents.Empty();
    Body->Stop();Body->SetAnimation(nullptr);Body->SetSkeletalMeshAsset(nullptr);AppliedIdle.Reset();
    FallbackBody->SetStaticMesh(nullptr);FallbackBody->SetVisibility(false);
    for(auto& Pair:Visuals)if(Pair.Value){RemoveInstanceComponent(Pair.Value);Pair.Value->DestroyComponent();}
    Visuals.Reset();Applied.Reset();
}
void AAetherCharacterPreviewActor::RenderFrame(float Dt,float Yaw,float Pitch,float Zoom)
{
    if(!Capture->TextureTarget)return;
    if(Body->GetSkeletalMeshAsset())
    {
        Body->TickAnimation(FMath::Clamp(Dt,0.f,.1f),false);Body->RefreshBoneTransforms();
        Body->UpdateComponentToWorld();Body->UpdateBounds();
    }
    FBox Bounds(ForceInit);
    if(Body->GetSkeletalMeshAsset())Bounds+=Body->Bounds.GetBox();
    else Bounds+=FallbackBody->Bounds.GetBox();
    for(const auto& Pair:Visuals){Pair.Value->UpdateComponentToWorld();Pair.Value->UpdateBounds();Bounds+=Pair.Value->Bounds.GetBox();}
    if(!Bounds.IsValid||Bounds.GetSize().ContainsNaN())Bounds=FBox(FVector(-50,-50,0),FVector(50,50,200));
    const FVector Center=Bounds.GetCenter();
    const float Aspect=float(Capture->TextureTarget->SizeX)/FMath::Max(1,Capture->TextureTarget->SizeY);
    const float HalfH=FMath::DegreesToRadians(Capture->FOVAngle*.5f),HalfV=FMath::Atan(FMath::Tan(HalfH)/Aspect);
    // 包围球涵盖头、脚及长武器末端；使用较窄视场角，初次打开默认完整入镜。
    const float Radius=FMath::Max(10.f,float(Bounds.GetExtent().Size()));
    const float Distance=Radius/FMath::Max(.05f,FMath::Sin(FMath::Min(HalfH,HalfV)))*1.12f*Zoom;
    const FVector Location=Center+FRotator(Pitch,Yaw,0).Vector()*Distance;
    Capture->SetWorldLocationAndRotation(Location,(Center-Location).Rotation());
    GetWorld()->SendAllEndOfFrameUpdates();Capture->CaptureScene();
}
