#pragma once
#include "Components/ActorComponent.h"
#include "AetherMotionTypes.h"
#include "AetherMotionComponent.generated.h"
class UAetherMotionProfile;
class USkeletalMeshComponent;
class UIKRetargeter;
struct FStreamableHandle;
UCLASS(ClassGroup=Animation,meta=(BlueprintSpawnableComponent))
class AETHERMOTIONRUNTIME_API UAetherMotionComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UAetherMotionComponent();
    UPROPERTY(EditAnywhere) TSoftObjectPtr<UAetherMotionProfile> ProfileAsset;
    // 游戏适配器只写通用意图；组件不推断伤害、技能、任务或存档规则。
    void SetIntent(bool bAllowed,FName Style,FGuid Action);
    void InvalidateMotion();
    FAetherMotionClipPtr PoseClip() const{return Clip;}
    double PoseFrame() const{return Frame;}
    FMatrix PoseBasis() const;
    float GeneratedWeight() const{return Weight;}
    USkeletalMeshComponent* GetSourceMesh() const{return SourceMesh;}
    UIKRetargeter* GetRetargeter() const;
    FString Status() const;
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* Function) override;
private:
    void LoadAssets();
    void AssetsReady();
    UPROPERTY(Transient) TObjectPtr<UAetherMotionProfile> Profile;
    UPROPERTY(Transient) TObjectPtr<USkeletalMeshComponent> SourceMesh;
    TSharedPtr<FStreamableHandle> Loading;
    FAetherMotionStamp Stamp;
    FAetherMotionClipPtr Clip;
    FVector LastPosition=FVector::ZeroVector,LastVelocity=FVector::ZeroVector,LastFacing=FVector::ForwardVector;
    FName DesiredStyle=TEXT("Idle");
    bool bAllowed=false,bWasAllowed=false,bTransitionRequested=false;
    uint64 AssetGeneration=0;
    FName BodyProfile;
    uint64 Agent=0,AcceptedSequence=0;
    double Frame=3,NextPlan=0;
    float Weight=0;
    int32 LastBackend=-1,StableResults=0;
    FString Message=TEXT("动作资源准备中");
};
