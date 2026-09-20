#include "AetherEncounters.h"
#include "Characters/AetherFrontierCharacter.h"

bool AAetherEncounterDirector::RestoreNative(const FAetherEncounterStateV10& A,const FAetherEncounterStateV10& R,FString& Reason)
{
    if(!HasAuthority()||!AbbeyEnemies.IsEmpty()||!RelayEnemies.IsEmpty()||AbbeyChanneler||RelayChanneler)
    {Reason=TEXT("Encounter restore requires an unstarted authoritative director");return false;}
    const auto Convert=[this](const FAetherEncounterStateV10& S,FName Id)
    {
        FAetherEncounterRun V;V.Definition=Id;V.Instance=S.Instance;V.Phase=EAetherEncounterPhase(S.Phase);
        V.Version=S.Version;V.Wave=S.Wave;V.LockedSeats=S.LockedSeats;V.Progress=S.Progress;
        // 存档中的 WorldTime 属于旧进程。当前进程重新给本阶段完整上限，不能立即误判超时。
        V.PhaseStarted=GetWorld()->GetTimeSeconds();V.Participants=S.Participants;V.Settled=S.Settled;return V;
    };
    Abbey=Convert(A,"Abbey");Relay=Convert(R,"Relay");EmptySinceAbbey=EmptySinceRelay=0;
    for(auto* Run:{&Abbey,&Relay})
    {
        if(Run->Phase==EAetherEncounterPhase::Idle||Run->Phase==EAetherEncounterPhase::Failed||Run->Phase==EAetherEncounterPhase::Succeeded)continue;
        auto& Enemies=Run==&Abbey?AbbeyEnemies:RelayEnemies;
        // Relay 引导阶段已打完三波，不能调用 SpawnWave 把 Relay0 再生成一次。
        if(Run==&Relay&&Run->Phase==EAetherEncounterPhase::Channel)continue;
        SpawnWave(*Run,Enemies);
        if(Run->Phase==EAetherEncounterPhase::Failed){Reason=TEXT("Saved encounter wave has no matching definition");return false;}
    }
    ForceNetUpdate();Reason.Reset();return true;
}
