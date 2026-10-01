#include "AI/AetherNpcPerception.h"
#include "AI/AetherNpcPerceptionDefinitions.h"
#include "AI/AetherNpcSkillDecision.h"
#include "Combat/AetherCombat.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "EngineUtils.h"

void FAetherNpcPerception::Reset()
{Observation={};Observer.Reset();ProfileId.Reset();NextSampleAt=LastUpdateAt=0;}
FAetherNpcObservation FAetherNpcPerception::Observe(AAetherCharacter& C,const FAetherNpcPerceptionProfile& Profile)
{
    if(!IsValid(&C)||C.IsActorBeingDestroyed()||!C.GetWorld()||C.GetActorLocation().ContainsNaN()){Reset();return {};}
    const double Now=C.CombatTime();
    if(!Profile.IsValid()||!FAetherNpcSkillDecision::IsControlled(C)||!FMath::IsFinite(Now)||C.Home.ContainsNaN())
    {Reset();return {};}
    if(Observer.Get()!=C.GetController()||!ProfileId.Equals(Profile.Id,ESearchCase::CaseSensitive)||Now<LastUpdateAt)
    {Reset();Observer=C.GetController();ProfileId=Profile.Id;}
    LastUpdateAt=Now;
    if(FVector::DistSquared(C.GetActorLocation(),C.Home)>FMath::Square(Profile.SelfLeashRadiusCm))
    {Observation={};return Observation;}
    if(auto* Target=Observation.Target.Get();!Target||!Target->HasActorBegunPlay()||!FAetherNpcSkillDecision::CanTarget(C,*Target)||Now-Observation.LastSeenAt>Profile.MemorySeconds)
        Observation={};
    if(Now>=NextSampleAt)
    {
        NextSampleAt=Now+Profile.SampleIntervalSeconds;Observation.bVisible=false;
        FVector Eye;FRotator View;C.GetActorEyesViewPoint(Eye,View);
        double Best=FMath::Square(Profile.SightRadiusCm);
        if(!Eye.ContainsNaN())for(TActorIterator<AAetherCharacter> It(C.GetWorld());It;++It)
        {
            auto* Candidate=*It;if(!Candidate->HasActorBegunPlay()||!FAetherNpcSkillDecision::CanTarget(C,*Candidate))continue;
            const FVector Position=Candidate->GetActorLocation();
            const double Distance=FVector::DistSquared(Position,C.GetActorLocation());
            if(Position.ContainsNaN()||Distance>=Best||FVector::DistSquared(Position,C.Home)>=FMath::Square(Profile.TargetHomeRadiusCm))continue;
            FVector TargetEye;FRotator TargetView;Candidate->GetActorEyesViewPoint(TargetEye,TargetView);
            FCollisionQueryParams Query(SCENE_QUERY_STAT(AetherNpcPerception),false,&C);Query.AddIgnoredActor(Candidate);
            if(TargetEye.ContainsNaN()||C.GetWorld()->LineTraceTestByChannel(Eye,TargetEye,ECC_Visibility,Query))continue;
            Best=Distance;Observation.Target=Candidate;Observation.LastSeenPosition=Position;Observation.LastSeenAt=Now;Observation.bVisible=true;
        }
    }
    // 超过新鲜度仅保留搜索事实；采样失败立即不可见，不能用宽限偷读遮挡后的坐标。
    if(Now-Observation.LastSeenAt>Profile.ObservationFreshnessSeconds)Observation.bVisible=false;
    return Observation;
}
