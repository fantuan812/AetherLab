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

namespace
{
bool SameView(const FAetherStartupView& A,const FAetherStartupView& B)
{
    return A.LocalAttemptToken==B.LocalAttemptToken&&A.ServerAttemptId==B.ServerAttemptId&&A.Stage==B.Stage&&A.FailureCode==B.FailureCode&&
        A.bCanStart==B.bCanStart&&A.bCanCancel==B.bCanCancel&&A.bCanRetry==B.bCanRetry&&
        A.StartIssue==B.StartIssue&&A.CancelIssue==B.CancelIssue&&A.RetryIssue==B.RetryIssue;
}
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
    TWeakObjectPtr<UPendingNetGame> PendingConnection;
    FDelegateHandle PreLoad,PostLoad,NetworkFailure,TravelFailure;
    uint32 ServerSequence=0;
    FString RetryURL,FrontendPackage,PlayablePackage;
    bool bRetryRemote=false,bFrontendAvailable=false,bPlayableAvailable=false,bRouting=false,bTravelQueued=false;
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
    {RefreshActions();if(!SameView(Before,View))Owner.OnChanged.Broadcast();}
    void Begin(EAetherStartupStage Stage,bool KeepFailure)
    {
        const auto Failure=KeepFailure?View.FailureCode:EAetherStartupFailure::None;
        View={};View.LocalAttemptToken=FGuid::NewGuid();View.Stage=Stage;View.FailureCode=Failure;
        Controller.Reset();ServerSequence=0;
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
        if(!bTravelQueued&&View.Stage==EAetherStartupStage::Idle&&IsFrontend(W))View.Stage=EAetherStartupStage::Frontend;
        if(!bTravelQueued&&W->GetNetMode()!=NM_Client&&W->GetAuthGameMode<AAetherFrontierMode>())
            if(auto* P=GI()->GetSubsystem<UAetherNativePersistence>())Accept(P->StartupStatus());
        if(View.Stage==EAetherStartupStage::WorldReady&&!bTravelQueued)
        {
            auto* PC=Cast<AAetherPlayerController>(GI()->GetFirstLocalPlayerController(W));
            auto* LP=PC?PC->GetLocalPlayer():nullptr;auto* C=PC?Cast<AAetherFrontierCharacter>(PC->GetPawn()):nullptr;
            auto* Commands=LP?LP->GetSubsystem<UAetherCommandClient>():nullptr;
            if(C&&Commands&&Commands->GetChannel().IsValid()&&Commands->GetProfile().IsSet()&&
                Commands->GetProfile()->CharacterId.Equals(Commands->GetOwnerIdentity(),ESearchCase::CaseSensitive)&&
                C->ProfileState()&&C->ProfileState()->Profile.CharacterId.Equals(Commands->GetOwnerIdentity(),ESearchCase::CaseSensitive)&&
                C->BuffRuntime->PresentationReady(Commands->GetProfile()->Revision)&&!C->bTravelPending&&C->Ready())View.Stage=EAetherStartupStage::Ready;
        }
        Publish(Before);
    }
};

UAetherStartupClient::UAetherStartupClient()=default;
UAetherStartupClient::~UAetherStartupClient()=default;
void FAetherStartupClientImplDeleter::operator()(FAetherStartupClientImpl* Value) const{delete Value;}
const FAetherStartupView& UAetherStartupClient::GetView() const
{static const FAetherStartupView Empty;return Impl?Impl->View:Empty;}
void UAetherStartupClient::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);Impl.Reset(new FAetherStartupClientImpl(*this));Impl->ResolveRoutes();
    Impl->PreLoad=FCoreUObjectDelegates::PreLoadMapWithContext.AddWeakLambda(this,[this](const FWorldContext& Context,const FString&)
    {
        if(!Impl||Context.OwningGameInstance!=GetGameInstance())return;
        const auto Before=Impl->View;
        if(!Impl->bTravelQueued)Impl->Begin(EAetherStartupStage::Connecting,Terminal(Impl->View.Stage));
        Impl->bTravelQueued=true;
        Impl->Controller.Reset();Impl->ServerSequence=0;Impl->View.ServerAttemptId.Invalidate();Impl->Publish(Before);
    });
    Impl->PostLoad=FCoreUObjectDelegates::PostLoadMapWithWorld.AddWeakLambda(this,[this](UWorld* W)
    {if(Impl&&Impl->OwnWorld(W)){const auto Before=Impl->View;Impl->BindWorld(W);Impl->Publish(Before);}});
    if(GEngine)
    {
        Impl->NetworkFailure=GEngine->OnNetworkFailure().AddWeakLambda(this,[this](UWorld* W,UNetDriver* Driver,ENetworkFailure::Type,const FString&)
        {
            if(!Impl||Impl->bRouting||!Impl->OwnWorld(W)||GetWorld()!=W)return;
            const auto* Pending=GEngine->PendingNetGameFromWorld(W);
            const bool CurrentPending=Pending&&(!Driver||Pending->GetNetDriver()==Driver);
            if(!CurrentPending&&(Impl->bTravelQueued||Impl->World.Get()!=W||Driver!=W->GetNetDriver()))return;
            if(CurrentPending)Impl->Remember(Pending->URL);
            const auto Before=Impl->View;Impl->bTravelQueued=false;Impl->View.Stage=EAetherStartupStage::Failed;
            Impl->View.FailureCode=EAetherStartupFailure::NetworkFailure;Impl->Publish(Before);
        });
        Impl->TravelFailure=GEngine->OnTravelFailure().AddWeakLambda(this,[this](UWorld* W,ETravelFailure::Type,const FString&)
        {
            if(!Impl||Impl->bRouting||!Impl->OwnWorld(W)||GetWorld()!=W)return;
            const auto Before=Impl->View;Impl->bTravelQueued=false;Impl->View.Stage=EAetherStartupStage::Failed;
            Impl->View.FailureCode=EAetherStartupFailure::TravelFailure;Impl->Publish(Before);
        });
    }
}
void UAetherStartupClient::ReceiveStartup(AAetherPlayerController* C,const FAetherStartupSnapshot& Snapshot)
{
    if(!Impl||!IsValid(C)||C->IsActorBeingDestroyed()||!C->IsLocalController()||!C->GetLocalPlayer()||
        C->GetGameInstance()!=GetGameInstance()||!Impl->OwnWorld(C->GetWorld())||GetWorld()!=C->GetWorld()||
        C->GetLocalPlayer()->GetPlayerController(C->GetWorld())!=C)return;
    const auto Before=Impl->View;
    if(Impl->World.Get()!=C->GetWorld())Impl->BindWorld(C->GetWorld());
    if(Impl->bRouting||Impl->bTravelQueued||Terminal(Impl->View.Stage))return;
    // 同世界也可能先替换当前PC、后销毁旧PC；前置LocalPlayer核对已经排除了旧连接。
    // 新Controller必须可接收同一服务端阶段，但旧按钮/缓存身份不能跨连接继续使用。
    if(!Impl->Controller.IsExplicitlyNull()&&Impl->Controller.Get()!=C)Impl->Begin(EAetherStartupStage::Connecting,false);
    Impl->Controller=C;Impl->Accept(Snapshot);Impl->Publish(Before);
}
bool UAetherStartupClient::RequestStart(FGuid Token)
{
    if(!Impl||Token!=Impl->View.LocalAttemptToken||!Token.IsValid())return false;
    const auto Before=Impl->View;Impl->ResolveRoutes();Impl->Publish(Before);
    if(!Impl||Token!=Impl->View.LocalAttemptToken||!Impl->View.bCanStart)return false;
    const FString URL=Impl->PlayablePackage;Impl->RetryURL=URL;Impl->bRetryRemote=false;
    return Impl->QueueTravel(URL,false);
}
bool UAetherStartupClient::RequestCancel(FGuid Token)
{
    if(!Impl||Token!=Impl->View.LocalAttemptToken||!Token.IsValid())return false;
    const auto Before=Impl->View;Impl->ResolveRoutes();Impl->Publish(Before);
    if(!Impl||Token!=Impl->View.LocalAttemptToken||!Impl->View.bCanCancel)return false;
    return Impl->QueueTravel(Impl->FrontendPackage,true);
}
bool UAetherStartupClient::RequestRetry(FGuid Token)
{
    if(!Impl||Token!=Impl->View.LocalAttemptToken||!Token.IsValid())return false;
    const auto Before=Impl->View;Impl->ResolveRoutes();Impl->Publish(Before);
    if(!Impl||Token!=Impl->View.LocalAttemptToken||!Impl->View.bCanRetry)return false;
    const FString URL=Impl->RetryURL;return Impl->QueueTravel(URL,false);
}
void UAetherStartupClient::Tick(float){if(Impl)Impl->Tick();}
bool UAetherStartupClient::IsTickable() const{return !IsTemplate()&&Impl.IsValid();}
TStatId UAetherStartupClient::GetStatId() const{RETURN_QUICK_DECLARE_CYCLE_STAT(UAetherStartupClient,STATGROUP_Tickables);}
UWorld* UAetherStartupClient::GetTickableGameObjectWorld() const{return GetWorld();}
void UAetherStartupClient::Deinitialize()
{
    if(Impl)
    {
        FCoreUObjectDelegates::PreLoadMapWithContext.Remove(Impl->PreLoad);FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(Impl->PostLoad);
        if(GEngine){GEngine->OnNetworkFailure().Remove(Impl->NetworkFailure);GEngine->OnTravelFailure().Remove(Impl->TravelFailure);}
        Impl.Reset();
    }
    OnChanged.Clear();Super::Deinitialize();
}
