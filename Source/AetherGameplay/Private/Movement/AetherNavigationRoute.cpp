#include "Movement/AetherNavigationRoute.h"
#include "NavigationSystem.h"
#include "NavigationData.h"
#include "NavigationSystemTypes.h"
#include "GameFramework/Character.h"

void FAetherNavigationRoute::Clear(EAetherNavigationRouteStatus Reason)
{
    if(NativePath.IsValid()||Status!=Reason)++Revision;
    NativePath.Reset();PointIndex=0;Status=Reason;
}

void FAetherNavigationRoute::Query(ACharacter& Character,const FVector& LocalGoal,const FVector& ProjectionExtent)
{
    auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(Character.GetWorld());
    const auto* Data=Nav?Nav->GetNavDataForProps(Character.GetNavAgentPropertiesRef(),Character.GetNavAgentLocation()):nullptr;
    if(!Data)
    {
        const bool Missing=!Nav||!Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate);
        if(Missing&&Status!=EAetherNavigationRouteStatus::MissingNavigation)
            UE_LOG(LogTemp,Warning,TEXT("AETHER_NAVIGATION_MISSING %s: 保留旧局部steering；此状态不代表正式地图导航已就绪。"),*Character.GetName());
        Clear(Missing?EAetherNavigationRouteStatus::MissingNavigation:EAetherNavigationRouteStatus::UnsupportedAgent);return;
    }
    FNavLocation End;
    if(!Nav->ProjectPointToNavigation(LocalGoal,End,ProjectionExtent,Data))
    {Clear(EAetherNavigationRouteStatus::ProjectionFailed);return;}
    FPathFindingQuery Request(&Character,*Data,Character.GetActorLocation(),End.Location);
    Request.SetNavAgentProperties(Character.GetNavAgentPropertiesRef());Request.SetAllowPartialPaths(true);
    const auto Result=Nav->FindPathSync(Request,EPathFindingMode::Regular);
    AcceptQueryResult(Result);
}

void FAetherNavigationRoute::AcceptQueryResult(const FPathFindingResult& Result)
{
    const auto& Path=Result.Path;
    if(!Result.IsSuccessful()||!Path.IsValid()||!Path->IsValid()||!Path->IsUpToDate())
    {Clear(EAetherNavigationRouteStatus::QueryFailed);return;}
    // 本组件按既有节流主动查询；原生路径只报告失效，不能另启自动重寻调度。
    Path->EnableRecalculationOnInvalidation(false);
    NativePath=Path;PointIndex=0;++Revision;
    Status=Path->IsPartial()?EAetherNavigationRouteStatus::Partial:EAetherNavigationRouteStatus::Complete;
}

bool FAetherNavigationRoute::RefreshValidity()
{
    if(Status!=EAetherNavigationRouteStatus::Complete&&Status!=EAetherNavigationRouteStatus::Partial)return false;
    if(!NativePath.IsValid()||!NativePath->IsValid()||!NativePath->IsUpToDate())
    {Clear(EAetherNavigationRouteStatus::Invalidated);return false;}
    return true;
}

bool FAetherNavigationRoute::TakeWaypoint(const FVector& Position,double AcceptanceRadius,FVector& OutWaypoint)
{
    OutWaypoint=FVector::ZeroVector;
    if(!RefreshValidity())return false;
    const auto& Points=NativePath->GetPathPoints();
    while(Points.IsValidIndex(PointIndex)&&FVector::DistSquared2D(Position,Points[PointIndex].Location)<FMath::Square(AcceptanceRadius))++PointIndex;
    if(!Points.IsValidIndex(PointIndex))
    {
        Clear(Status==EAetherNavigationRouteStatus::Partial?EAetherNavigationRouteStatus::PartialFinished:EAetherNavigationRouteStatus::CompleteSegmentFinished);
        return false;
    }
    OutWaypoint=Points[PointIndex].Location;return true;
}
