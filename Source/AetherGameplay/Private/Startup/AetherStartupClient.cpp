#include "Startup/AetherStartupClient.h"
#include "Startup/AetherStartupSettings.h"
#include "Persistence/AetherNativePersistence.h"
#include "Networking/AetherCommandClient.h"
#include "Framework/AetherPlayerController.h"
#include "Framework/AetherFrontendMode.h"
#include "Framework/AetherFrontierMode.h"
#include "Framework/AetherProgression.h"
#include "Characters/AetherFrontierCharacter.h"
#include "Effects/AetherBuffRuntime.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/NetDriver.h"
#include "Engine/PendingNetGame.h"
#include "Engine/World.h"
#include "UObject/UObjectGlobals.h"

bool AetherStartup::SameView(const FAetherStartupView& A,const FAetherStartupView& B)
{
    return A.LocalAttemptToken==B.LocalAttemptToken&&A.ServerAttemptId==B.ServerAttemptId&&A.Stage==B.Stage&&A.FailureCode==B.FailureCode&&
        A.bCanStart==B.bCanStart&&A.bCanCancel==B.bCanCancel&&A.bCanRetry==B.bCanRetry&&
        A.StartIssue==B.StartIssue&&A.CancelIssue==B.CancelIssue&&A.RetryIssue==B.RetryIssue;
}
namespace
{
bool ServerStage(EAetherStartupStage Stage)
{
    return Stage==EAetherStartupStage::WaitingForBackend||Stage==EAetherStartupStage::ReadingStorage||
        Stage==EAetherStartupStage::Auditing||Stage==EAetherStartupStage::Restoring||Stage==EAetherStartupStage::WorldReady||
        Stage==EAetherStartupStage::Failed||Stage==EAetherStartupStage::Cancelled;
}
bool Terminal(EAetherStartupStage Stage)
{return Stage==EAetherStartupStage::Failed||Stage==EAetherStartupStage::Cancelled;}
}

struct FAetherStartupClientImpl
{
    UAetherStartupClient& Owner;
    FAetherStartupView View;
    TWeakObjectPtr<UWorld> World;
    TWeakObjectPtr<AAetherPlayerController> Controller;
    TWeakObjectPtr<AAetherPlayerController> CommandController;
    FGuid CommandChannel;
    TWeakObjectPtr<UPendingNetGame> PendingConnection;
    FDelegateHandle PreLoad,PostLoad,NetworkFailure,TravelFailure;
    uint32 ServerSequence=0;
    FString RetryURL,FrontendPackage,PlayablePackage;
    bool bRetryRemote=false,bFrontendAvailable=false,bPlayableAvailable=false,bRouting=false,bTravelQueued=false,bStopped=false;
    explicit FAetherStartupClientImpl(UAetherStartupClient& In):Owner(In){}
    UGameInstance* GI() const{return Owner.GetGameInstance();}
    bool OwnWorld(UWorld* W) const
    {return W&&W->GetGameInstance()==GI()&&(W->WorldType==EWorldType::Game||W->WorldType==EWorldType::PIE);}
    bool IsFrontend(UWorld* W) const{return OwnWorld(W)&&W->GetAuthGameMode<AAetherFrontendMode>()!=nullptr;}
    void ResolveRoutes()
    {
        const auto* Settings=GetDefault<UAetherStartupSettings>();
        bFrontendAvailable=Settings->ResolveMap(true,FrontendPackage);
        bPlayableAvailable=Settings->ResolveMap(false,PlayablePackage);
        if(bFrontendAvailable&&bPlayableAvailable&&FrontendPackage==PlayablePackage)
        {bFrontendAvailable=false;bPlayableAvailable=false;}
    }
    void RefreshActions()
    {
        using R=EAetherStartupRouteIssue;
        View.bCanStart=View.bCanCancel=View.bCanRetry=false;
        View.StartIssue=View.CancelIssue=View.RetryIssue=R::Busy;
        auto* W=GI()->GetWorld();
        if(!OwnWorld(W)||W->GetNetMode()==NM_DedicatedServer)
        {View.StartIssue=View.CancelIssue=View.RetryIssue=R::NoLocalWorld;return;}
        if(bRouting)return;
        const bool Frontend=IsFrontend(W)&&View.Stage==EAetherStartupStage::Frontend;
        if(Frontend)
        {
            View.StartIssue=bPlayableAvailable?R::None:R::PlayableUnavailable;View.bCanStart=View.StartIssue==R::None;
            View.RetryIssue=RetryURL.IsEmpty()?R::NoRetryTarget:(!bRetryRemote&&!bPlayableAvailable?R::PlayableUnavailable:R::None);
            View.bCanRetry=View.RetryIssue==R::None;
        }
        else if(View.Stage!=EAetherStartupStage::Idle&&View.Stage!=EAetherStartupStage::Ready&&
            (!bTravelQueued||View.Stage!=EAetherStartupStage::Cancelled))
        {
            View.CancelIssue=bFrontendAvailable?R::None:R::FrontendUnavailable;View.bCanCancel=View.CancelIssue==R::None;
        }
    }
    void Publish(const FAetherStartupView& Before)
    {if(bStopped)return;RefreshActions();if(!AetherStartup::SameView(Before,View))Owner.OnChanged.Broadcast();}
    void Begin(EAetherStartupStage Stage,bool KeepFailure)
    {
        const auto Failure=KeepFailure?View.FailureCode:EAetherStartupFailure::None;
        View={};View.LocalAttemptToken=FGuid::NewGuid();View.Stage=Stage;View.FailureCode=Failure;
        Controller.Reset();CommandController.Reset();CommandChannel.Invalidate();ServerSequence=0;
    }
    void Remember(const FURL& URL)
    {
        // 仅复制本 GI 已交给引擎解析的目标；接口没有从网络失败文本或 RPC 接收 URL 的入口。
        if(!URL.Valid)return;
        if(!URL.Host.IsEmpty())
        {if(URL.Port<=0||URL.Port>65535)return;RetryURL=URL.ToString();bRetryRemote=true;return;}
        if(bPlayableAvailable&&URL.Map==PlayablePackage){RetryURL=URL.ToString();bRetryRemote=false;}
    }
    void BindWorld(UWorld* W)
    {
        if(!OwnWorld(W))return;
        const bool Changed=World.Get()!=W;
        World=W;Controller.Reset();PendingConnection.Reset();bTravelQueued=false;
        if(Changed||!View.LocalAttemptToken.IsValid())Begin(EAetherStartupStage::Connecting,Terminal(View.Stage)||View.FailureCode!=EAetherStartupFailure::None);
        ResolveRoutes();
        if(IsFrontend(W))View.Stage=EAetherStartupStage::Frontend;
        else if(W->GetNetMode()==NM_Client||W->GetAuthGameMode<AAetherFrontierMode>())
        {
            View.Stage=EAetherStartupStage::Connecting;
            if(const auto* Context=GEngine?GEngine->GetWorldContextFromWorld(W):nullptr)
                if(Context->OwningGameInstance==GI())Remember(W->GetNetMode()==NM_Client?Context->LastRemoteURL:Context->LastURL);
        }
        else View.Stage=EAetherStartupStage::Idle;
    }
    void Accept(const FAetherStartupSnapshot& S)
    {
        if(bRouting||bTravelQueued||Terminal(View.Stage)||View.Stage==EAetherStartupStage::Frontend||
            !S.AttemptId.IsValid()||S.Sequence==0||!ServerStage(S.Stage)||uint8(S.FailureCode)>uint8(EAetherStartupFailure::TravelFailure)||
            (S.Stage!=EAetherStartupStage::Failed&&S.FailureCode!=EAetherStartupFailure::None))return;
        if(View.ServerAttemptId.IsValid()&&View.ServerAttemptId!=S.AttemptId)return;
        if(S.Sequence<=ServerSequence)return;
        View.ServerAttemptId=S.AttemptId;ServerSequence=S.Sequence;View.Stage=S.Stage;View.FailureCode=S.FailureCode;
    }
    void SynchronizeController(AAetherPlayerController* Current)
    {
        if(bRouting||bTravelQueued||View.Stage==EAetherStartupStage::Cancelled)return;
        if(Current&&(!IsValid(Current)||Current->IsActorBeingDestroyed()))Current=nullptr;
        if(!Controller.IsExplicitlyNull()&&Controller.Get()!=Current)Begin(EAetherStartupStage::Connecting,false);
        Controller=Current;
    }
    bool QueueTravel(const FString& URL,bool Cancel)
    {
        auto* W=GI()->GetWorld();if(!GEngine||!OwnWorld(W)||bRouting||(bTravelQueued&&!Cancel)||URL.IsEmpty())return false;
        const FString Destination=URL;
        TGuardValue<bool> Guard(bRouting,true);
        const auto Before=View;
        const FGuid ServerAttempt=View.ServerAttemptId;
        Begin(Cancel?EAetherStartupStage::Cancelled:EAetherStartupStage::Connecting,Cancel);
        bTravelQueued=true;
        // 无PC也只取消当前GI的pending connection，绝不触碰其他PIE/窗口。
        if(GEngine->PendingNetGameFromWorld(W))GEngine->CancelPending(W,nullptr);
        if(Cancel&&W->GetNetMode()!=NM_Client)
            if(auto* P=GI()->GetSubsystem<UAetherNativePersistence>())P->CancelPreparation(W,ServerAttempt);
        if(bStopped||GI()->GetWorld()!=W)return false;
        GEngine->SetClientTravel(W,*Destination,TRAVEL_Absolute);
        Publish(Before);return true;
    }
    void Tick()
    {
        const auto Before=View;auto* W=GI()->GetWorld();
        if(!OwnWorld(W)){Publish(Before);return;}
        if(World.Get()!=W)BindWorld(W);
        if(auto* Pending=GEngine?GEngine->PendingNetGameFromWorld(W):nullptr)
        {
            if(PendingConnection.Get()!=Pending)
            {
                PendingConnection=Pending;
                if(!Pending->bLoadedMapSuccessfully)
                {
                    if(!bTravelQueued)Begin(EAetherStartupStage::Connecting,false);
                    bTravelQueued=true;
                }
                Remember(Pending->URL);
            }
        }
        if(!IsFrontend(W))SynchronizeController(Cast<AAetherPlayerController>(GI()->GetFirstLocalPlayerController(W)));
        if(!bTravelQueued&&View.Stage==EAetherStartupStage::Idle&&IsFrontend(W))View.Stage=EAetherStartupStage::Frontend;
        if(!bTravelQueued&&W->GetNetMode()!=NM_Client&&W->GetAuthGameMode<AAetherFrontierMode>())
            if(auto* P=GI()->GetSubsystem<UAetherNativePersistence>())Accept(P->StartupStatus());
        if(View.Stage==EAetherStartupStage::WorldReady&&!bTravelQueued)
        {
            auto* PC=Cast<AAetherPlayerController>(GI()->GetFirstLocalPlayerController(W));
            auto* LP=PC?PC->GetLocalPlayer():nullptr;auto* C=PC?Cast<AAetherFrontierCharacter>(PC->GetPawn()):nullptr;
            auto* Commands=LP?LP->GetSubsystem<UAetherCommandClient>():nullptr;
            if(C&&Commands&&Owner.OwnsCommandChannel(PC,Commands->GetChannel())&&Commands->GetProfile().IsSet()&&
                Commands->GetProfile()->CharacterId.Equals(Commands->GetOwnerIdentity(),ESearchCase::CaseSensitive)&&
                C->ProfileState()&&C->ProfileState()->Profile.CharacterId.Equals(Commands->GetOwnerIdentity(),ESearchCase::CaseSensitive)&&
                C->BuffRuntime->PresentationReady(Commands->GetProfile()->Revision)&&!C->bTravelPending&&C->Ready())View.Stage=EAetherStartupStage::Ready;
        }
        Publish(Before);
    }
};

UAetherStartupClient::UAetherStartupClient()=default;
UAetherStartupClient::~UAetherStartupClient()=default;
const FAetherStartupView& UAetherStartupClient::GetView() const
{static const FAetherStartupView Empty;return Impl?Impl->View:Empty;}
void UAetherStartupClient::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);Impl=MakeShared<FAetherStartupClientImpl>(*this);Impl->ResolveRoutes();
    Impl->PreLoad=FCoreUObjectDelegates::PreLoadMapWithContext.AddWeakLambda(this,[this](const FWorldContext& Context,const FString&)
    {
        const auto Active=Impl;
        if(!Active||Context.OwningGameInstance!=GetGameInstance())return;
        const auto Before=Active->View;
        if(!Active->bTravelQueued)Active->Begin(EAetherStartupStage::Connecting,Terminal(Active->View.Stage));
        Active->bTravelQueued=true;
        Active->Controller.Reset();Active->ServerSequence=0;Active->View.ServerAttemptId.Invalidate();Active->Publish(Before);
    });
    Impl->PostLoad=FCoreUObjectDelegates::PostLoadMapWithWorld.AddWeakLambda(this,[this](UWorld* W)
    {const auto Active=Impl;if(Active&&Active->OwnWorld(W)){const auto Before=Active->View;Active->BindWorld(W);Active->Publish(Before);}});
    if(GEngine)
    {
        Impl->NetworkFailure=GEngine->OnNetworkFailure().AddWeakLambda(this,[this](UWorld* W,UNetDriver* Driver,ENetworkFailure::Type,const FString&)
        {
            const auto Active=Impl;
            if(!Active||Active->bRouting||!Active->OwnWorld(W)||GetWorld()!=W)return;
            const auto* Pending=GEngine->PendingNetGameFromWorld(W);
            const bool CurrentPending=Pending&&(!Driver||Pending->GetNetDriver()==Driver);
            if(!CurrentPending&&(Active->bTravelQueued||Active->World.Get()!=W||Driver!=W->GetNetDriver()))return;
            if(CurrentPending)Active->Remember(Pending->URL);
            const auto Before=Active->View;Active->bTravelQueued=false;Active->View.Stage=EAetherStartupStage::Failed;
            Active->View.FailureCode=EAetherStartupFailure::NetworkFailure;Active->Publish(Before);
        });
        Impl->TravelFailure=GEngine->OnTravelFailure().AddWeakLambda(this,[this](UWorld* W,ETravelFailure::Type,const FString&)
        {
            const auto Active=Impl;
            if(!Active||Active->bRouting||!Active->OwnWorld(W)||GetWorld()!=W)return;
            const auto Before=Active->View;Active->bTravelQueued=false;Active->View.Stage=EAetherStartupStage::Failed;
            Active->View.FailureCode=EAetherStartupFailure::TravelFailure;Active->Publish(Before);
        });
    }
}
void UAetherStartupClient::ReceiveStartup(AAetherPlayerController* C,const FAetherStartupSnapshot& Snapshot)
{
    const auto Active=Impl;
    if(!Active||!IsValid(C)||C->IsActorBeingDestroyed()||!C->IsLocalController()||!C->GetLocalPlayer()||
        C->GetGameInstance()!=GetGameInstance()||!Active->OwnWorld(C->GetWorld())||GetWorld()!=C->GetWorld()||
        C->GetLocalPlayer()->GetPlayerController(C->GetWorld())!=C)return;
    const auto Before=Active->View;
    if(Active->World.Get()!=C->GetWorld())Active->BindWorld(C->GetWorld());
    if(Impl!=Active||Active->bRouting||Active->bTravelQueued||Active->View.Stage==EAetherStartupStage::Cancelled)return;
    // 同世界也可能先替换当前PC、后销毁旧PC；前置LocalPlayer核对已经排除了旧连接。
    // 新Controller必须可接收同一服务端阶段，但旧按钮/缓存身份不能跨连接继续使用。
    Active->SynchronizeController(C);Active->Accept(Snapshot);Active->Publish(Before);
}
void UAetherStartupClient::ObserveCommandChannel(AAetherPlayerController* C,FGuid Channel)
{
    const auto Active=Impl;
    if(!Active||!IsValid(C)||C->IsActorBeingDestroyed()||!C->IsLocalController()||!C->GetLocalPlayer()||
        C->GetGameInstance()!=GetGameInstance()||GetWorld()!=C->GetWorld()||!Active->OwnWorld(C->GetWorld())||
        C->GetLocalPlayer()->GetPlayerController(C->GetWorld())!=C||Active->bRouting||Active->bTravelQueued||
        Active->View.Stage==EAetherStartupStage::Cancelled)return;
    const auto Before=Active->View;if(Active->World.Get()!=C->GetWorld())Active->BindWorld(C->GetWorld());
    Active->SynchronizeController(C);
    if(Active->CommandController.Get()!=C||Active->CommandChannel!=Channel)
    {
        Active->View.LocalAttemptToken=FGuid::NewGuid();Active->CommandController=C;Active->CommandChannel=Channel;
        if(Active->View.Stage==EAetherStartupStage::Ready)Active->View.Stage=EAetherStartupStage::WorldReady;
    }
    Active->Publish(Before);
}
bool UAetherStartupClient::OwnsCommandChannel(AAetherPlayerController* C,FGuid Channel) const
{
    return Impl&&IsValid(C)&&Channel.IsValid()&&Impl->Controller.Get()==C&&Impl->CommandController.Get()==C&&
        Impl->CommandChannel==Channel&&Impl->World.Get()==C->GetWorld()&&GetWorld()==C->GetWorld();
}
bool UAetherStartupClient::RequestStart(FGuid Token)
{
    const auto Active=Impl;
    if(!Active||Token!=Active->View.LocalAttemptToken||!Token.IsValid())return false;
    const auto Before=Active->View;Active->ResolveRoutes();Active->Publish(Before);
    if(Impl!=Active||!Active||Token!=Active->View.LocalAttemptToken||!Active->View.bCanStart)return false;
    const FString URL=Active->PlayablePackage;Active->RetryURL=URL;Active->bRetryRemote=false;
    return Active->QueueTravel(URL,false);
}
bool UAetherStartupClient::RequestCancel(FGuid Token)
{
    const auto Active=Impl;
    if(!Active||Token!=Active->View.LocalAttemptToken||!Token.IsValid())return false;
    const auto Before=Active->View;Active->ResolveRoutes();Active->Publish(Before);
    if(Impl!=Active||!Active||Token!=Active->View.LocalAttemptToken||!Active->View.bCanCancel)return false;
    return Active->QueueTravel(Active->FrontendPackage,true);
}
bool UAetherStartupClient::RequestRetry(FGuid Token)
{
    const auto Active=Impl;
    if(!Active||Token!=Active->View.LocalAttemptToken||!Token.IsValid())return false;
    const auto Before=Active->View;Active->ResolveRoutes();Active->Publish(Before);
    if(Impl!=Active||!Active||Token!=Active->View.LocalAttemptToken||!Active->View.bCanRetry)return false;
    const FString URL=Active->RetryURL;return Active->QueueTravel(URL,false);
}
void UAetherStartupClient::Tick(float){const auto Active=Impl;if(Active)Active->Tick();}
bool UAetherStartupClient::IsTickable() const{return !IsTemplate()&&Impl.IsValid();}
TStatId UAetherStartupClient::GetStatId() const{RETURN_QUICK_DECLARE_CYCLE_STAT(UAetherStartupClient,STATGROUP_Tickables);}
UWorld* UAetherStartupClient::GetTickableGameObjectWorld() const{return GetWorld();}
void UAetherStartupClient::Deinitialize()
{
    if(Impl)
    {
        Impl->bStopped=true;
        FCoreUObjectDelegates::PreLoadMapWithContext.Remove(Impl->PreLoad);FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(Impl->PostLoad);
        if(GEngine){GEngine->OnNetworkFailure().Remove(Impl->NetworkFailure);GEngine->OnTravelFailure().Remove(Impl->TravelFailure);}
        Impl.Reset();
    }
    OnChanged.Clear();Super::Deinitialize();
}
