#include "Preview/AetherCharacterPreviewSubsystem.h"
#include "Preview/AetherCharacterPreviewActor.h"
#include "Combat/AetherCombat.h"
#include "Characters/AetherFrontierCharacter.h"
#include "GameFramework/PlayerController.h"
#include "Assets/AetherContent.h"
#include "AetherEquipmentVisuals.h"
#include "Presentation/AetherMenuSubsystem.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/LocalPlayer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimSequence.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "PreviewScene.h"
#include "RHI.h"
#include "Misc/App.h"
#include "Subsystems/SubsystemCollection.h"

// 此快照只含外观值和软资源身份，异步回调不会再读取可能已销毁的真实 Pawn/Equipment。
struct FAetherPreviewAppearance
{
    TSoftObjectPtr<USkeletalMesh> Body;
    TSoftObjectPtr<UAnimSequence> Idle;
    FRotator Rotation=FRotator::ZeroRotator;
    TArray<FAetherEquipmentVisualSpec> Equipment;
    TSoftObjectPtr<UStaticMesh> Fallback{FSoftObjectPath(TEXT("/Engine/BasicShapes/Cube.Cube"))};
    TSoftObjectPtr<UMaterialInterface> Material;
};
struct FAetherPreviewSceneOwner
{
    // 禁用游戏实例、物理、导航和音频；预览世界没有区域流送、NPC、持久化服务。
    FPreviewScene Value{FPreviewScene::ConstructionValues().SetEditor(false).SetCreatePhysicsScene(false)
        .ShouldSimulatePhysics(false).SetTransactional(false).AllowAudioPlayback(false).SetForceMipsResident(false)
        .SetLightBrightness(4.f).SetSkyBrightness(.8f)};
};
UAetherCharacterPreviewSubsystem::UAetherCharacterPreviewSubsystem()
{
    PreviewMaterial=TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/UI/Materials/M_CharacterPreview.M_CharacterPreview")));
}
UAetherCharacterPreviewSubsystem::~UAetherCharacterPreviewSubsystem()=default;
void UAetherCharacterPreviewSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);Collection.InitializeDependency<UAetherMenuSubsystem>();
    Menu=GetLocalPlayer()->GetSubsystem<UAetherMenuSubsystem>();
    if(Menu.IsValid())Menu->OnChanged.AddUObject(this,&UAetherCharacterPreviewSubsystem::HandleMenuContext);
}
void UAetherCharacterPreviewSubsystem::Deinitialize()
{
    Close();if(Menu.IsValid())Menu->OnChanged.RemoveAll(this);Menu.Reset();OnChanged.Clear();
    PreviewActor=nullptr;Scene.Reset();Super::Deinitialize();
}
void UAetherCharacterPreviewSubsystem::HandleMenuContext()
{
    if(bActive&&(!Menu.IsValid()||Menu->GetBoundPawn()!=Source.Get()||Menu->GetPage()!=EAetherMenuPage::Inventory))Close();
}
bool UAetherCharacterPreviewSubsystem::EnsureScene()
{
    if(!Scene)Scene.Reset(new FAetherPreviewSceneOwner());
    if(!Scene->Value.IsInitialized())return false;
    if(!IsValid(PreviewActor))
    {
        FActorSpawnParameters P;P.ObjectFlags|=RF_Transient;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        PreviewActor=Scene->Value.GetWorld()->SpawnActor<AAetherCharacterPreviewActor>(FVector::ZeroVector,FRotator::ZeroRotator,P);
    }
    if(!RenderTarget)
    {
        // 固定 768×1024、RGBA16f，单目标约 6 MiB。窗口拉伸不会无限扩张 GPU 分配。
        RenderTarget=NewObject<UTextureRenderTarget2D>(this);
        RenderTarget->RenderTargetFormat=ETextureRenderTargetFormat::RTF_RGBA16f;
        RenderTarget->ClearColor=FLinearColor(0,0,0,1);
        RenderTarget->bAutoGenerateMips=false;RenderTarget->InitAutoFormat(768,1024);
    }
    return IsValid(PreviewActor)&&RenderTarget!=nullptr;
}
void UAetherCharacterPreviewSubsystem::Acquire(UObject* Requester,AAetherCharacter* Character)
{
    if(!IsValid(Requester))return;
    if(!Character){Release(Requester);return;}
    if(ViewOwner.Get()!=Requester)Close();
    if(Source.Get()!=Character)SetSource(Character);
    ViewOwner=Requester;
}
void UAetherCharacterPreviewSubsystem::Release(UObject* Requester)
{
    // 已移除的旧页面不能关闭新页面刚接管的同一预览场景。
    if(ViewOwner.Get()==Requester)Close();
}
void UAetherCharacterPreviewSubsystem::SetSource(AAetherCharacter* Character)
{
    if(Source.Get()==Character)return;
    Close();if(!IsValid(Character))return;
    auto* PC=GetLocalPlayer()->GetPlayerController(GetWorld());
    if(!PC||PC->GetPawn()!=Character)return;Source=Character;
    if(!FApp::CanEverRender()||GUsingNullRHI)
    {Status=FText::FromString(TEXT("当前运行模式不提供角色预览。"));OnChanged.Broadcast();return;}
    bActive=true;ResetView();
    Character->OnAppearanceChanged.AddUObject(this,&UAetherCharacterPreviewSubsystem::RefreshAppearance);
    EquipmentSource=Character->Equipment;
    if(EquipmentSource.IsValid())EquipmentSource->OnLoadoutChanged.AddDynamic(this,&UAetherCharacterPreviewSubsystem::RefreshAppearance);
    RefreshAppearance();
}
void UAetherCharacterPreviewSubsystem::RefreshAppearance()
{
    auto* C=Source.Get();if(!bActive||!C)return;
    const uint64 Request=++RequestVersion;
    if(LoadHandle){LoadHandle->CancelHandle();LoadHandle.Reset();}
    FAetherPreviewAppearance Appearance;Appearance.Material=PreviewMaterial;
    if(const auto* D=C->CharacterDefinition.Get())
    {Appearance.Body=D->BodyMesh;Appearance.Idle=D->PreviewIdleAnimation;Appearance.Rotation=D->MeshRotation;}
    if(C->Equipment)Appearance.Equipment=AetherEquipmentVisuals::Resolve(C->Equipment->Catalog,C->Equipment->Slots);
    TArray<FSoftObjectPath> Paths;
    auto Add=[&](const FSoftObjectPath& Path){if(Path.IsValid()&&!Path.ResolveObject())Paths.AddUnique(Path);};
    Add(Appearance.Body.ToSoftObjectPath());Add(Appearance.Idle.ToSoftObjectPath());
    Add(Appearance.Material.ToSoftObjectPath());Add(Appearance.Fallback.ToSoftObjectPath());
    for(const auto& Spec:Appearance.Equipment)Add(Spec.Mesh.ToSoftObjectPath());
    bReady=false;Status=FText::FromString(TEXT("正在载入角色外观…"));OnChanged.Broadcast();
    if(Paths.IsEmpty()){ApplyLoaded(Appearance,Request);return;}
    LoadHandle=UAssetManager::GetStreamableManager().RequestAsyncLoad(Paths,
        FStreamableDelegate::CreateWeakLambda(this,[this,Appearance,Request](){ApplyLoaded(Appearance,Request);}));
    if(!LoadHandle){ApplyLoaded(Appearance,Request);}
}
void UAetherCharacterPreviewSubsystem::ApplyLoaded(const FAetherPreviewAppearance& Appearance,uint64 Request)
{
    // 异步资源可能晚于切页、换 Pawn 或第二次装备提交；旧请求只能丢弃，不能重新激活预览。
    if(!bActive||!Source.IsValid()||RequestVersion!=Request)return;
    auto* PC=GetLocalPlayer()->GetPlayerController(GetWorld());
    if(!PC||PC->GetPawn()!=Source.Get()){Close();return;}
    if(!EnsureScene()){Status=FText::FromString(TEXT("无法创建独立角色预览场景。"));OnChanged.Broadcast();return;}
    FString Warning=PreviewActor->ApplyAppearance(Appearance.Body.Get(),Appearance.Rotation,Appearance.Idle.Get(),Appearance.Equipment,Appearance.Fallback.Get());
    if(auto* Material=Appearance.Material.Get())
    {
        DisplayMaterial=UMaterialInstanceDynamic::Create(Material,this);
        DisplayMaterial->SetTextureParameterValue(TEXT("PreviewTexture"),RenderTarget);
    }
    else {DisplayMaterial=nullptr;Warning+=TEXT(" 透明预览材质缺失，暂用不透明背景。");}
    PreviewActor->SetRenderTarget(RenderTarget,DisplayMaterial!=nullptr);
    Warning=Warning.TrimStartAndEnd();
    if(!Warning.IsEmpty()&&ReportedWarnings.Num()<64&&!ReportedWarnings.Contains(Warning))
    {
        ReportedWarnings.Add(Warning);
        UE_LOG(LogTemp,Warning,TEXT("Aether character preview: %s"),*Warning);
    }
    Status=Warning.IsEmpty()?FText::FromString(TEXT("拖动旋转 · 滚轮缩放 · 手柄右摇杆旋转")):FText::FromString(Warning);
    bReady=true;SinceCapture=1.f/30.f;LoadHandle.Reset();OnChanged.Broadcast();
}
void UAetherCharacterPreviewSubsystem::Close()
{
    ++RequestVersion;bActive=false;bReady=false;SinceCapture=0;
    if(LoadHandle){LoadHandle->CancelHandle();LoadHandle.Reset();}
    if(Source.IsValid())Source->OnAppearanceChanged.RemoveAll(this);
    if(EquipmentSource.IsValid())EquipmentSource->OnLoadoutChanged.RemoveDynamic(this,&UAetherCharacterPreviewSubsystem::RefreshAppearance);
    Source.Reset();EquipmentSource.Reset();ViewOwner.Reset();
    if(IsValid(PreviewActor))PreviewActor->ClearAppearance();
    DisplayMaterial=nullptr;
    // 保留一个空的预览世界以避免每次关菜单触发 FPreviewScene 析构中的全量 GC。
    // 昂贵的模型引用、材质和 RT 都释放；退出 LocalPlayer 时才销毁场景。
    if(RenderTarget)RenderTarget->ReleaseResource();RenderTarget=nullptr;Status=FText::GetEmpty();OnChanged.Broadcast();
}
bool UAetherCharacterPreviewSubsystem::IsTickable() const
{return !IsTemplate()&&bActive;}
TStatId UAetherCharacterPreviewSubsystem::GetStatId() const
{RETURN_QUICK_DECLARE_CYCLE_STAT(UAetherCharacterPreviewSubsystem,STATGROUP_Tickables);}
UWorld* UAetherCharacterPreviewSubsystem::GetTickableGameObjectWorld() const{return GetWorld();}
void UAetherCharacterPreviewSubsystem::Tick(float Dt)
{
    if(!IsTickable())return;
    // 即使资源正在加载也检查上下文，避免 Pawn 销毁后因 bReady=false 永久保留订阅。
    auto* PC=GetLocalPlayer()->GetPlayerController(GetWorld());
    if(!ViewOwner.IsValid()||!Source.IsValid()||!PC||PC->GetPawn()!=Source.Get()){Close();return;}
    if(!bReady||!IsValid(PreviewActor)||!RenderTarget)return;
    SinceCapture+=FMath::Clamp(Dt,0.f,.1f);if(SinceCapture<1.f/30.f)return;
    const float Elapsed=SinceCapture;SinceCapture=0;
    PreviewActor->RenderFrame(Elapsed,OrbitYaw,OrbitPitch,ZoomFactor);
}
void UAetherCharacterPreviewSubsystem::Rotate(float YawDelta,float PitchDelta)
{
    if(!bActive||!FMath::IsFinite(YawDelta)||!FMath::IsFinite(PitchDelta))return;
    OrbitYaw=FMath::UnwindDegrees(OrbitYaw+FMath::Clamp(YawDelta,-90.f,90.f));
    OrbitPitch=FMath::Clamp(OrbitPitch+FMath::Clamp(PitchDelta,-45.f,45.f),-20.f,25.f);
    SinceCapture=1.f/30.f;
}
void UAetherCharacterPreviewSubsystem::Zoom(float Delta)
{if(bActive&&FMath::IsFinite(Delta)){ZoomFactor=FMath::Clamp(ZoomFactor+Delta,.8f,2.f);SinceCapture=1.f/30.f;}}
void UAetherCharacterPreviewSubsystem::ResetView()
{OrbitYaw=0;OrbitPitch=0;ZoomFactor=1;SinceCapture=1.f/30.f;}

void FAetherPreviewSceneOwnerDeleter::operator()(FAetherPreviewSceneOwner* Value) const { delete Value; }
