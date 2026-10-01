#pragma once
#include "CoreMinimal.h"
class AAetherCharacter;
class AController;
struct FAetherNpcPerceptionProfile;

struct FAetherNpcObservation
{
    TWeakObjectPtr<AAetherCharacter> Target;
    FVector LastSeenPosition=FVector::ZeroVector;
    double LastSeenAt=0;
    bool bVisible=false;
};
// 每个角色一个采样历史，由原 Think 调度。不存在独立 Tick 或全局目标/威胁表。
class AETHERGAMEPLAY_API FAetherNpcPerception
{
public:
    FAetherNpcObservation Observe(AAetherCharacter& Character,const FAetherNpcPerceptionProfile& Profile);
    void Reset();
private:
    FAetherNpcObservation Observation;
    TWeakObjectPtr<AController> Observer;
    FString ProfileId;
    double NextSampleAt=0,LastUpdateAt=0;
};
