#include "AetherFrontier.h"
#include "Framework/AetherPlayerController.h"
#include "World/AetherNativeContainer.h"
#include "Persistence/AetherNativePersistence.h"
#include "Persistence/AetherNativeWorldPhysics.h"
#include "Profile/AetherProfileCodec.h"
#include "Definitions/AetherV10Definitions.h"
#include "Inventory/AetherResourceGate.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "AetherAssetPreload.h"
#include "AetherWorldDefinition.h"
#include "ReactiveWorldSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Kismet/GameplayStatics.h"

namespace
{
FAetherEncounterRun EncounterView(const FAetherEncounterStateV10& S)
{
    FAetherEncounterRun R;R.Definition=FName(*S.Definition);R.Instance=S.Instance;R.Phase=EAetherEncounterPhase(S.Phase);
    R.Version=S.Version;R.Wave=S.Wave;R.LockedSeats=S.LockedSeats;R.PhaseStarted=S.PhaseStarted;R.Progress=S.Progress;
    R.Participants=S.Participants;R.Settled=S.Settled;return R;
}
}
void AAetherFrontierMode::FailNativeScene(const FString& Reason)
{
    bWorldRestoreFailed=true;bNativeSceneReady=false;
    if(bNativeFailureReported)return;bNativeFailureReported=true;
    UE_LOG(LogTemp,Error,TEXT("AETHER_NATIVE_SCENE_FAILED %s"),*Reason);
    // 失败不会清库或回退旧档；每个已入场连接均退出，避免操作半恢复场景。
    for(auto It=GetWorld()->GetPlayerControllerIterator();It;++It)
        if(auto* PC=It->Get())PC->ClientReturnToMainMenuWithTextReason(FText::FromString(TEXT("世界存档恢复失败，请检查服务器日志并保留存档。")));
}
void AAetherFrontierMode::PublishNativeWorld(const FAetherWorldStateV10& W)
{
    if(NativeWorld.IsSet()&&W.Revision<NativeWorld->Revision)return;
    NativeWorld=W;
    auto* S=GetGameState<AAetherFrontierState>();
    if(S){S->NativeWorldRevision=W.Revision;S->bSupplyRestored=W.bSupplyRestored;S->bWorkshopRestored=W.bWorkshopRestored;
        S->bBridgeReleased=W.bBridgeReleased;S->bPowerOn=W.bPowerOn;S->bRain=W.RainKgPerM2Sec>0;S->ForceNetUpdate();}
    // 下列兼容值只用于尚未搬迁的导航/营地/区域查询，不再交给旧 writer。
    Database->bSupplyRestored=W.bSupplyRestored;Database->bWorkshopRestored=W.bWorkshopRestored;
    Database->bBridgeReleased=W.bBridgeReleased;Database->bPowerOn=W.bPowerOn;
    Database->AmbientTemperatureC=W.AmbientTemperatureC;Database->RainKgPerM2Sec=W.RainKgPerM2Sec;Database->WindMPerSec=W.WindMPerSec;
    Database->World.Reset();for(const auto& B:W.Bodies)Database->World.Add(AetherNativeWorldPhysics::ToRuntimeRecord(B));
    Database->WorldFacts.Sources.Reset();for(const auto& Fact:W.WorldFactSources)Database->WorldFacts.Sources.Add(FName(*Fact.Key),FName(*Fact.Value));
    Database->Abbey=EncounterView(W.Abbey);Database->Relay=EncounterView(W.Relay);
    Database->CampReceipts.Reset();for(const auto& C:W.CampReceipts)
    {FAetherCampReceipt V;V.Definition=FName(*C.Definition);V.Instance=C.Instance;V.RespawnAfterUtc=C.RespawnAfterUtc;Database->CampReceipts.Add(V);}
    Database->Loot.Reset();for(const auto& L:W.Loot)
    {FAetherWorldLoot V;V.ClaimId=L.ClaimId;V.Location=L.Location;V.Definition=FName(*L.Definition);V.Count=L.Count;V.ClaimedBy=L.ClaimedBy;
        for(const auto& I:L.Items)V.Items.Add(FName(*I.Key),I.Value);Database->Loot.Add(V);}
    // 提交发布不再次 Restore 物理体/遭遇，避免覆盖提交期间仍在进行的模拟。
}
bool AAetherFrontierMode::RestoreNativeScene(const FAetherWorldStateV10& W,const TMap<FString,int64>& Profiles,
    const TArray<FAetherContainerRestoreDescriptor>& Containers,FString& Reason)
{
    if(!HasAuthority()||bNativeSceneReady||!Encounters||!Database){Reason=TEXT("Scene not assembled for native restore");return false;}
    const auto& D=FAetherV10Definitions::Get();
    if(!W.Validate(D.Items,D.Rules,Profiles,Reason)||!AetherNativeWorldPhysics::RestoreLoaded(*GetWorld(),W,Reason))return false;
    PublishNativeWorld(W);
    if(!Encounters->RestoreNative(W.Abbey,W.Relay,Reason))return false;
    for(const auto& C:Containers)if(C.bActive)
    {
        if(NativeContainers.Contains(C.Id)){Reason=TEXT("Duplicate native container scene identity");return false;}
        auto* A=GetWorld()->SpawnActor<AAetherNativeContainer>();
        if(!A||!A->Configure(C)){if(A)A->Destroy();Reason=TEXT("Cannot restore native container");return false;}
        NativeContainers.Add(C.Id,A);
    }
    for(const auto& L:Database->Loot)if(L.ClaimedBy.IsEmpty())SpawnLoot(L);
    // 地图新增的热源只初始化一次；已有持久记录绝不能重新点燃。
    for(const auto& E:FAetherWorldDefinitions::Get().Objects)
        if(E.Heat>0&&!W.Bodies.ContainsByPredicate([&](const auto& B){return B.StableId.Equals(E.Id.ToString(),ESearchCase::CaseSensitive);}))
            if(auto* A=Prop(E.Id)){FReactiveStimulus H;H.HeatJ=E.Heat;A->Reactive->Inject(H);}
    for(AAetherFrontierProp* A:Props)if(IsValid(A))
    {A->bWasBurning=A->Reactive->State.bBurning;A->bExtinguished=A->bInspectableFire&&!A->bWasBurning&&A->Reactive->State.FuelKg<A->Reactive->GetMaterial().InitialFuelKg-1.e-8;}
    if(auto* Source=Prop("PowerSource"))Source->Mechanism->bPowerEnabled=W.bPowerOn;
    Reason.Reset();return true;
}
bool AAetherFrontierMode::PublishNativeState(AAetherPlayerController& PC,const FAetherProfileStateV10& P,
    const FAetherWorldStateV10* W,const FAetherContainerStateV10* Container)
{
    auto* PS=PC.GetPlayerState<AAetherPlayerState>();auto* Pawn=Cast<AAetherFrontierCharacter>(PC.GetPawn());
    if(!bNativeSceneReady||bWorldRestoreFailed||!PS||!Pawn||Pawn->ProfileState()!=PS)return false;
    FString Why;if(!PS->PublishNativeProfile(P,Why))return false;
    if(W)PublishNativeWorld(*W);
    if(Container)
    {
        auto* Existing=NativeContainers.Find(Container->ContainerId);
        auto* A=Existing?Existing->Get():nullptr;
        if(!IsValid(A)&&Container->bActive)
        {
            FAetherContainerRestoreDescriptor D{Container->ContainerId,Container->OwnerCharacterId,Container->RegionId,
                Container->Kind,Container->Location,true,Container->Revision};
            A=GetWorld()->SpawnActor<AAetherNativeContainer>();
            if(!A||!A->Configure(D)){if(A)A->Destroy();return false;}
            NativeContainers.Add(Container->ContainerId,A);
        }
        if(IsValid(A))
        {
            if(A->bOnlyRelevantToOwner&&A->OwnerCharacterId.Equals(P.CharacterId,ESearchCase::CaseSensitive))A->SetOwner(&PC);
            if(!A->Publish(*Container))return false;
            if(!Container->bActive)NativeContainers.Remove(Container->ContainerId);
        }
    }
    if(P.Claims.Contains(TEXT("Q_Main_03")))
        for(TActorIterator<AAetherFrontierCharacter> It(GetWorld());It;++It)
            if(It->Reactive->bOwnerOnlyStimuli&&It->GetOwner()==Pawn)It->Pacify();
    return true;
}
void AAetherFrontierMode::BeginNativeLogin(AAetherPlayerController* PC)
{
    if(!IsValid(PC)||NativeLogins.Contains(PC)||NativePlayersReady.Contains(PC)||NativeLoginRejected.Contains(PC)||!bNativeSceneReady)return;
    auto* PS=PC->GetPlayerState<AAetherPlayerState>();if(!PS||PS->Profile.CharacterId.IsEmpty())return;
    NativeLogins.Add(PC,GetGameInstance()->GetSubsystem<UAetherNativePersistence>()->LoadOrCreateProfile(PS->Profile.CharacterId));
}
void AAetherFrontierMode::TickNativeStartup()
{
    if(!bNativeMode||bWorldRestoreFailed)return;
    auto* Persistence=GetGameInstance()->GetSubsystem<UAetherNativePersistence>();
    if(!Persistence){FailNativeScene(TEXT("Missing native persistence subsystem"));return;}
    if(Persistence->Phase()==EAetherNativePersistencePhase::Failed){FailNativeScene(Persistence->Failure());return;}
    if(!bNativeSceneReady)
    {
        auto* Assets=GetWorld()->GetSubsystem<UAetherAssetPreload>();
        if(Persistence->Phase()!=EAetherNativePersistencePhase::Prepared||!Encounters||!Assets||!Assets->Ready())return;
        // 同一游戏线程内完成恢复和后端安装；所有回调只捕获弱 GameMode，旅行后不会访问旧世界。
        const TWeakObjectPtr<AAetherFrontierMode> Self=this;FString Why;
        if(!Persistence->Activate(
            [Self](auto& PC,const auto& C,const auto& P,auto& X){return Self.IsValid()&&Self->ResolveNativeContext(PC,C,P,X);},
            [Self](auto& PC,const auto& P,const auto* W,const auto* C){return Self.IsValid()&&Self->PublishNativeState(PC,P,W,C);},
            [Self](const auto& W,const auto& P,const auto& C,FString& R){return Self.IsValid()&&Self->RestoreNativeScene(W,P,C,R);},Why))
        {FailNativeScene(Why);return;}
        bNativeSceneReady=true;Encounters->SetActorTickEnabled(true);
        Persistence->ConfigureCheckpoints(
            [Self](const auto& Before,auto& Next,FString& R){return Self.IsValid()&&Self->CaptureNativeWorld(Before,Next,R);},
            [Self](const auto& W){if(Self.IsValid())Self->PublishNativeWorld(W);});
        UE_LOG(LogTemp,Display,TEXT("AETHER_V10_SCENE_READY revision=%lld"),NativeWorld->Revision);
    }
    for(auto It=GetWorld()->GetPlayerControllerIterator();It;++It)BeginNativeLogin(Cast<AAetherPlayerController>(It->Get()));
    TArray<TWeakObjectPtr<AAetherPlayerController>> Completed;
    for(auto& Pair:NativeLogins)
    {
        auto* PC=Pair.Key.Get();
        if(!PC){Completed.Add(Pair.Key);continue;}
        if(!Pair.Value.IsValid()||!Pair.Value.IsReady())continue;
        auto R=Pair.Value.Get();Completed.Add(Pair.Key);auto* PS=PC->GetPlayerState<AAetherPlayerState>();
        const auto& D=FAetherV10Definitions::Get();FAetherProfileStateV10 P;FString Why;
        if(!PS||R.Code!=EAetherStoreCode::Found||!R.Value.IsSet()||
            !AetherProfileCodec::Decode(R.Value->Payload,D.Items,D.Skills,D.Rules,P,Why)||!PS->PublishNativeProfile(P,Why))
        {NativeLoginRejected.Add(PC);PC->ClientReturnToMainMenuWithTextReason(FText::FromString(TEXT("角色存档暂不可用，请稍后重新连接。")));continue;}
        NativePlayersReady.Add(PC);
        for(auto& C:NativeContainers)if(IsValid(C.Value)&&C.Value->bOnlyRelevantToOwner&&C.Value->OwnerCharacterId.Equals(P.CharacterId,ESearchCase::CaseSensitive))C.Value->SetOwner(PC);
        RestartPlayer(PC);
    }
    for(const auto& PC:Completed)NativeLogins.Remove(PC);
}

void AAetherFrontierMode::EndPlay(const EEndPlayReason::Type Reason)
{
    if(bNativeMode)
    {
        bNativeSceneReady=false;
        if(auto* GI=GetGameInstance())GI->GetSubsystem<UAetherNativePersistence>()->ReleaseScene(GetWorld());
        NativeLogins.Reset();NativePlayersReady.Reset();NativeContainerSessions.Reset();NativeRegionSave={};
    }
    Super::EndPlay(Reason);
}
