#include "Movement/AetherNavigationProbe.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"

bool AetherNavigationProbe::ReadGeometry(const ACharacter& Character,FAetherNavigationProbeGeometry& Out)
{
    Out=FAetherNavigationProbeGeometry();
    const auto* Capsule=Character.GetCapsuleComponent();const auto* Movement=Character.GetCharacterMovement();
    if(!IsValid(&Character)||Character.IsActorBeingDestroyed()||!Character.GetWorld()||!IsValid(Capsule)||!IsValid(Movement)||
        !Capsule->IsRegistered()||!Movement->IsRegistered()||!Capsule->IsQueryCollisionEnabled()||!Movement->HasValidData()||
        Movement->UpdatedComponent!=Capsule||Movement->GetCharacterOwner()!=&Character)return false;
    Capsule->GetScaledCapsuleSize(Out.Radius,Out.HalfHeight);
    Out.Center=Capsule->GetComponentLocation();Out.Rotation=Capsule->GetComponentQuat();Out.Channel=Capsule->GetCollisionObjectType();
    return FMath::IsFinite(Out.Radius)&&FMath::IsFinite(Out.HalfHeight)&&Out.Radius>0&&Out.HalfHeight>=Out.Radius&&
        !Out.Center.ContainsNaN()&&!Out.Rotation.ContainsNaN()&&Out.Rotation.IsNormalized();
}
bool AetherNavigationProbe::IsLocalStepClear(const ACharacter& Character,FVector Delta,FHitResult& OutFloor)
{
    OutFloor=FHitResult();FAetherNavigationProbeGeometry Geometry;
    if(Delta.ContainsNaN()||!ReadGeometry(Character,Geometry))return false;
    const auto* Movement=Character.GetCharacterMovement();const auto* World=Character.GetWorld();
    const FVector Down=Movement->GetGravityDirection();
    if(Down.ContainsNaN()||!Down.IsNormalized()||!FMath::IsFinite(Movement->MaxStepHeight)||Movement->MaxStepHeight<0)return false;
    Delta=FVector::VectorPlaneProject(Delta,Down);if(Delta.IsNearlyZero())return false;

    // 地面距离与坡度都由真实 CMC 判定，包含组件自身的坡度覆写与碰撞过滤。
    // 使用引擎支撑间距移开贴地接触，保留完整胶囊，不能忽略整块地面而漏掉同组件的墙。
    const float Distance=Movement->MaxStepHeight+UCharacterMovementComponent::MAX_FLOOR_DIST;
    FFindFloorResult StartFloor,EndFloor;
    Movement->ComputeFloorDist(Geometry.Center,Distance,Distance,StartFloor,Geometry.Radius,nullptr);
    Movement->ComputeFloorDist(Geometry.Center+Delta,Distance,Distance,EndFloor,Geometry.Radius,nullptr);
    const auto ValidFloor=[&](const FFindFloorResult& Floor)
    {
        const auto* Component=Floor.HitResult.GetComponent();
        return Floor.IsWalkableFloor()&&Movement->IsWalkable(Floor.HitResult)&&IsValid(Component)&&Component->IsRegistered()&&
            Component->IsQueryCollisionEnabled()&&FMath::IsFinite(Floor.GetDistanceToFloor());
    };
    if(!ValidFloor(StartFloor)||!ValidFloor(EndFloor))return false;
    const float Lift=FMath::Max(0.f,UCharacterMovementComponent::MIN_FLOOR_DIST-StartFloor.GetDistanceToFloor());
    // 只修正引擎允许的贴地间距；深陷地面的身体不是可通行起点。
    if(Lift>UCharacterMovementComponent::MAX_FLOOR_DIST)return false;
    const FVector Start=Geometry.Center-Down*Lift;
    const FVector End=Geometry.Center+Delta+Down*(EndFloor.GetDistanceToFloor()-UCharacterMovementComponent::MIN_FLOOR_DIST);
    if(Start.ContainsNaN()||End.ContainsNaN())return false;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(AetherNavigationProbe),false,&Character);
    FCollisionResponseParams Responses;Movement->InitCollisionParams(Query,Responses);
    const auto Shape=FCollisionShape::MakeCapsule(Geometry.Radius,Geometry.HalfHeight);
    if(World->OverlapBlockingTestByChannel(Start,Geometry.Rotation,Geometry.Channel,Shape,Query,Responses)||
        World->SweepTestByChannel(Start,End,Geometry.Rotation,Geometry.Channel,Shape,Query,Responses))return false;
    OutFloor=EndFloor.HitResult;return true;
}
