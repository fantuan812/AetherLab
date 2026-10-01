#pragma once
#include "CoreMinimal.h"

class ACharacter;
struct FNavigationPath;
struct FPathFindingResult;

enum class EAetherNavigationRouteStatus : uint8
{
    Empty, MissingNavigation, UnsupportedAgent, ProjectionFailed, QueryFailed,
    Complete, Partial, CompleteSegmentFinished, PartialFinished,
    Invalidated, LocalBlocked, GoalChanged, EscapingIce
};

// 单次原生导航查询的消费状态；不创建第二个移动调度器，不把局部探针当作整段可达证明。
struct AETHERGAMEPLAY_API FAetherNavigationRoute
{
    void Query(ACharacter& Character,const FVector& LocalGoal,const FVector& ProjectionExtent);
    void AcceptQueryResult(const FPathFindingResult& Result);
    void Clear(EAetherNavigationRouteStatus Reason=EAetherNavigationRouteStatus::Empty);
    bool RefreshValidity();
    bool TakeWaypoint(const FVector& Position,double AcceptanceRadius,FVector& OutWaypoint);
    bool AllowsDirectSteering() const { return Status==EAetherNavigationRouteStatus::MissingNavigation; }
    EAetherNavigationRouteStatus GetStatus() const { return Status; }
    uint32 GetRevision() const { return Revision; }
private:
    TSharedPtr<FNavigationPath,ESPMode::ThreadSafe> NativePath;
    int32 PointIndex=0;
    uint32 Revision=0;
    EAetherNavigationRouteStatus Status=EAetherNavigationRouteStatus::Empty;
};
