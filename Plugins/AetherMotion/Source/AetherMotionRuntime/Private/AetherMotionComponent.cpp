#include "AetherMotionComponent.h"
#include "AetherMotionProfile.h"
#include "AetherMotionWorld.h"
#include "AetherMotionSourceAnimInstance.h"
#include "MotionBricksScheduler.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Retargeter/IKRetargeter.h"
static TAutoConsoleVariable<int32> CVarAetherMotionBackend(TEXT("aether.Motion.Backend"),0,TEXT("0 traditional, 1 CPU, 2 Vulkan. Explicit validated profile selection."),ECVF_Default);
UAetherMotionComponent::UAetherMotionComponent()
{
    PrimaryComponentTick.bCanEverTick=true;PrimaryComponentTick.TickGroup=TG_PostPhysics;
    ProfileAsset=TSoftObjectPtr<UAetherMotionProfile>(FSoftObjectPath(TEXT("/Game/Animation/Motion/DA_MotionManny.DA_MotionManny")));
}
void UAetherMotionComponent::BeginPlay()
{
    Super::BeginPlay();if(GetNetMode()==NM_DedicatedServer){SetComponentTickEnabled(false);return;}
    Stamp.WorldEpoch=GetWorld()->GetSubsystem<UAetherMotionWorld>()->Epoch;Stamp.PawnEpoch=FGuid::NewGuid();
    Agent=AetherMotionScheduler().Register();LastPosition=GetOwner()->GetActorLocation();LoadAssets();
}
void UAetherMotionComponent::LoadAssets()
{
    if(ProfileAsset.IsNull())return;const TWeakObjectPtr<UAetherMotionComponent> Self=this;
    Loading=UAssetManager::GetStreamableManager().RequestAsyncLoad(ProfileAsset.ToSoftObjectPath(),[Self]()
    {
        if(!Self.IsValid()||!Self->Agent)return;
        Self->Profile=Self->ProfileAsset.Get();FString Why;
        if(!Self->Profile||!Self->Profile->Validate(Why)){Self->Message=Why.IsEmpty()?TEXT("动作配置资产缺失"):Why;return;}
        TArray<FSoftObjectPath> Paths={Self->Profile->SourceMesh.ToSoftObjectPath(),Self->Profile->Retargeter.ToSoftObjectPath()};
        Self->Loading=UAssetManager::GetStreamableManager().RequestAsyncLoad(Paths,[Self](){if(Self.IsValid()&&Self->Agent)Self->AssetsReady();});
    });
}
void UAetherMotionComponent::AssetsReady()
{
    if(!Profile||!Profile->SourceMesh.Get()||!Profile->Retargeter.Get())return;
    auto* Character=Cast<ACharacter>(GetOwner());if(!Character)return;
    SourceMesh=NewObject<USkeletalMeshComponent>(Character,TEXT("GeneratedMotionSource"));
    SourceMesh->SetupAttachment(Character->GetRootComponent());SourceMesh->SetAbsolute(false,false,false);
    SourceMesh->SetRelativeLocation(FVector(0,0,-Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()));
    SourceMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);SourceMesh->SetVisibility(false);SourceMesh->SetHiddenInGame(true);
    SourceMesh->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    SourceMesh->SetSkeletalMeshAsset(Profile->SourceMesh.Get());SourceMesh->SetAnimationMode(EAnimationMode::AnimationBlueprint);
    SourceMesh->SetAnimInstanceClass(UAetherMotionSourceAnimInstance::StaticClass());SourceMesh->RegisterComponent();
    SourceMesh->AddTickPrerequisiteComponent(this);Character->GetMesh()->AddTickPrerequisiteComponent(SourceMesh);
    Stamp.ProfileRevision=Profile->Revision;
}
void UAetherMotionComponent::SetIntent(bool Allowed,FName Style,FGuid Action)
{
    if(bAllowed!=Allowed||DesiredStyle!=Style||Stamp.ActionInstance!=Action)
    {bAllowed=Allowed;DesiredStyle=Style;Stamp.ActionInstance=Action;++Stamp.MovementRevision;StableResults=0;NextPlan=0;}
}
void UAetherMotionComponent::InvalidateMotion()
{
    ++Stamp.AgentGeneration;Clip.Reset();AcceptedSequence=0;Frame=3;Weight=0;StableResults=0;NextPlan=0;
}
FMatrix UAetherMotionComponent::PoseBasis() const{return Profile?Profile->Basis():FMatrix::Identity;}
UIKRetargeter* UAetherMotionComponent::GetRetargeter() const{return Profile?Profile->Retargeter.Get():nullptr;}
void UAetherMotionComponent::TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* Function)
{
    Super::TickComponent(Dt,Type,Function);if(!Agent||!Profile||!SourceMesh)return;
    auto* C=Cast<ACharacter>(GetOwner());if(!C)return;
    const int32 Backend=FMath::Clamp(CVarAetherMotionBackend.GetValueOnGameThread(),0,2);
    if(Backend!=LastBackend){LastBackend=Backend;InvalidateMotion();AetherMotionScheduler().Configure(EAetherMotionBackend(Backend),Profile->NativeThreads);}
    const FVector Position=C->GetActorLocation(),Velocity=C->GetVelocity(),Facing=C->GetActorForwardVector();
    if(FVector::DistSquared(Position,LastPosition)>FMath::Square(250.))InvalidateMotion();
    LastPosition=Position;
    if(FVector::DistSquared(Velocity,LastVelocity)>FMath::Square(30.)||Facing.Dot(LastFacing)<.985)
    {LastVelocity=Velocity;LastFacing=Facing;++Stamp.MovementRevision;NextPlan=0;}
    const bool Visible=C->IsLocallyControlled()||C->GetMesh()->WasRecentlyRendered(.3f);
    const bool Allowed=bAllowed&&Visible&&Backend!=0;
    // 兼容模式/屏幕外且没有淡出姿态时，不让隐藏 source 无条件评估整条动画图。
    SourceMesh->SetComponentTickEnabled(Allowed||Weight>0);
    SourceMesh->SetRelativeLocation(FVector(0,0,-C->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()));
    if(bWasAllowed!=Allowed){bWasAllowed=Allowed;InvalidateMotion();}
    const double Now=GetWorld()->GetTimeSeconds();
    if(Clip)Frame=FMath::Min(Frame+FMath::Max(0.f,Dt)*30.,double(Clip->Frames-1));
    FAetherMotionResult Result;
    if(AetherMotionScheduler().Take(Agent,Result))
    {
        if(Result.Stamp.SameIntent(Stamp)&&Result.Stamp.RequestSequence==Stamp.RequestSequence&&Allowed)
        {
            const double Age=Result.Clip?FMath::Max(0.,Now-Result.Clip->SimulationTime):0;
            if(Result.Clip&&Age<=Profile->MaxResultAge&&3+Age*30<Result.Clip->Frames-1)
            {Clip=Result.Clip;Frame=3+Age*30;AcceptedSequence=Result.Stamp.RequestSequence;++StableResults;Message=TEXT("生成动作");}
            else {StableResults=0;Message=Result.Reason.IsEmpty()?TEXT("生成结果超时，使用传统动作"):Result.Reason;NextPlan=Now+1;}
        }
    }
    const bool Usable=Allowed&&Clip&&Clip->Stamp.SameIntent(Stamp)&&Frame<Clip->Frames-1&&StableResults>=2;
    Weight=FMath::FInterpConstantTo(Weight,Usable?1.f:0.f,Dt,1.f/Profile->BlendSeconds);
    const double Lead=Clip?FMath::Clamp(Clip->InferenceSeconds+.12,.2,.6):.25;
    if(Allowed&&Now>=NextPlan&&!AetherMotionScheduler().IsPending(Agent)&&(!Clip||!Clip->Stamp.SameIntent(Stamp)||(Clip->Frames-Frame)/30.<Lead||StableResults<2))
    {
        const FString* Style=Profile->Styles.Find(DesiredStyle);if(!Style){Message=TEXT("当前动作使用传统动画");return;}
        FAetherMotionInput I;I.AgentId=Agent;I.Stamp=Stamp;I.Stamp.RequestSequence=++Stamp.RequestSequence;I.SimulationTime=Now;
        const FMatrix Inverse=Profile->Basis().InverseFast();
        I.Movement=FVector3f(Inverse.TransformVector(Velocity.GetSafeNormal()));I.Facing=FVector3f(Inverse.TransformVector(Facing));
        I.SpeedMeters=Velocity.Size2D()/100.f;I.Priority=C->IsLocallyControlled()?100.f:20.f;I.Style=*Style;
        I.AcceptedSequence=AcceptedSequence;I.ConsumedFrameIndex=uint32(FMath::FloorToInt(Frame));I.Seed=Stamp.RequestSequence;
        if(Clip&&Clip->Stamp.SameLifetime(Stamp))
        {
            I.Context=Clip->Boundary(I.ConsumedFrameIndex);
            // 水平轨迹只作为规划上下文；重新锚到真实胶囊位置，碰墙不会继续推进虚拟身体。
            const FVector Anchor=Inverse.TransformVector(Position/100.);
            if(I.Context.IsValid()){const float DX=Anchor.X-I.Context.Roots[9],DZ=Anchor.Z-I.Context.Roots[11];for(int32 F=0;F<4;++F){I.Context.Roots[F*3]+=DX;I.Context.Roots[F*3+2]+=DZ;}}
        }
        if(AetherMotionScheduler().Submit(MoveTemp(I)))NextPlan=Now+.1;else Message=AetherMotionScheduler().Diagnostic();
    }
}
void UAetherMotionComponent::EndPlay(const EEndPlayReason::Type Why)
{
    if(Loading){Loading->CancelHandle();Loading.Reset();}
    if(Agent){AetherMotionScheduler().Unregister(Agent);Agent=0;}
    Stamp.PawnEpoch.Invalidate();Clip.Reset();if(SourceMesh){SourceMesh->DestroyComponent();SourceMesh=nullptr;}
    Super::EndPlay(Why);
}
