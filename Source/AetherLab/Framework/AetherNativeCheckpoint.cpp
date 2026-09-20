#include "AetherFrontier.h"
#include "Persistence/AetherNativeWorldPhysics.h"

namespace
{
FAetherEncounterStateV10 CaptureRun(const FAetherEncounterRun& R,const FAetherEncounterStateV10& Previous)
{
    FAetherEncounterStateV10 S;S.Definition=R.Definition.ToString();S.Instance=R.Instance;S.Phase=uint8(R.Phase);
    S.Version=R.Version;S.Wave=R.Wave;S.LockedSeats=R.LockedSeats;S.PhaseStarted=R.PhaseStarted;S.Progress=R.Progress;
    S.Participants=R.Participants;S.Settled=R.Settled;
    // 奖励事务可能在本轮物理捕获前确认，单调合并同一实例的结算名单，绝不清掉持久成功回执。
    if(S.Instance==Previous.Instance)
        for(const auto& Id:Previous.Settled)if(S.Participants.Contains(Id))S.Settled.AddUnique(Id);
    return S;
}
}
bool AAetherFrontierMode::CaptureNativeWorld(const FAetherWorldStateV10& Previous,FAetherWorldStateV10& Candidate,FString& Reason)
{
    if(!bNativeSceneReady||bWorldRestoreFailed||!Encounters){Reason=TEXT("Native scene unavailable for checkpoint");return false;}
    // 存储线程可能已经确认机制事务，而游戏线程尚未轮询其回调。
    // 先发布新版本的离散意图，再采集现场，避免旧开关覆盖刚刚确认的操作。
    if(!NativeWorld.IsSet()||Previous.Revision>NativeWorld->Revision)PublishNativeWorld(Previous);
    if(!AetherNativeWorldPhysics::CaptureLoaded(*GetWorld(),Previous,Candidate,Reason))return false;
    // 世界事实、营地/掉落账本由各自事务维护；周期保存只拥有物理、天气和在运行的遭遇阶段。
    Candidate.Abbey=CaptureRun(Encounters->Abbey,Previous.Abbey);Candidate.Relay=CaptureRun(Encounters->Relay,Previous.Relay);
    if(auto* S=GetGameState<AAetherFrontierState>())Candidate.bBridgeReleased=Previous.bBridgeReleased||S->bBridgeReleased;
    return true;
}
