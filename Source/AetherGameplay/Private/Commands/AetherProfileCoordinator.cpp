#include "Commands/AetherProfileCoordinator.h"
#include "Profile/AetherProfileCodec.h"
namespace
{
EAetherCommandCode Code(EAetherStoreCode C)
{
    using E=EAetherStoreCode;using R=EAetherCommandCode;
    switch(C){case E::Expired:return R::Expired;case E::Conflict:return R::Conflict;case E::Missing:return R::Missing;
    case E::Invalid:return R::Invalid;case E::Busy:return R::Busy;default:return R::StorageUnavailable;}
}
bool CharacterId(const FString& S)
{
    if(S.IsEmpty()||S.Len()>32)return false;
    for(TCHAR C:S)if(C<32)return false;
    FTCHARToUTF8 U(*S);FUTF8ToTCHAR Back(U.Get(),U.Length());return FString(Back.Length(),Back.Get())==S;
}
}
struct FAetherProfileCoordinator::FImpl
{
    enum class EStage:uint8 {Receipt,Read,Commit,Refresh};
    struct FJob
    {
        FAetherProfileSession Session;FAetherPlayerCommand Command;FAetherCommandResult Result;
        EStage Stage=EStage::Receipt;
        TFuture<FAetherStoreResult> StoreFuture;
        TFuture<FAetherStoreReadResult> ReadFuture;
    };
    TSharedRef<IAetherTransactionalStore,ESPMode::ThreadSafe> Store;
    FAetherV10ItemDefinitions Items;FAetherSkillDefinitionsV10 Skills;FAetherRules Rules;
    TMap<FString,FAetherProfileSession> Sessions;
    TArray<TUniquePtr<FJob>> Jobs;
    bool bPolling=false;
    FImpl(TSharedRef<IAetherTransactionalStore,ESPMode::ThreadSafe> S,FAetherV10ItemDefinitions I,FAetherSkillDefinitionsV10 K,FAetherRules R)
        :Store(MoveTemp(S)),Items(MoveTemp(I)),Skills(MoveTemp(K)),Rules(MoveTemp(R)){}
    bool Current(const FAetherProfileSession& S) const
    {const auto* Bound=Sessions.Find(S.CharacterId);return Bound&&*Bound==S;}
    bool Decode(const FAetherStoreReadResult& Read,const FString& Actor,FAetherProfileStateV10& P) const
    {
        FString Reason;
        return Read.Code==EAetherStoreCode::Found&&Read.Value.IsSet()&&Read.Value->SchemaVersion==10&&
            AetherProfileCodec::Decode(Read.Value->Payload,Items,Skills,Rules,P,Reason)&&P.CharacterId==Actor&&P.Revision==Read.Value->Revision;
    }
    bool Receipt(FJob& J,const FAetherStoreResult& R)
    {
        FString Reason;FAetherCommandResult Result;
        if(!AetherCommands::DecodeResult(R.Result,Result,Reason)||Result.CommandId!=J.Command.CommandId||
            Result.FinalProfileRevision!=R.FinalProfileRevision||Result.Code!=EAetherCommandCode::Applied)
        {J.Result.Code=EAetherCommandCode::StorageUnavailable;return false;}
        J.Result=MoveTemp(Result);
        if(R.Code==EAetherStoreCode::Replayed)J.Result.Code=EAetherCommandCode::Replayed;
        J.Stage=EStage::Refresh;J.ReadFuture=Store->Read({EAetherAggregateKind::Profile,J.Session.CharacterId});return true;
    }
};
FAetherProfileCoordinator::FAetherProfileCoordinator(TSharedRef<IAetherTransactionalStore,ESPMode::ThreadSafe> Store,
    FAetherV10ItemDefinitions Items,FAetherSkillDefinitionsV10 Skills,FAetherRules Rules)
    :Impl(MakeUnique<FImpl>(MoveTemp(Store),MoveTemp(Items),MoveTemp(Skills),MoveTemp(Rules)))
{check(IsInGameThread());}
FAetherProfileCoordinator::~FAetherProfileCoordinator()
{
    check(IsInGameThread());
    // 丢弃 Future 不撤销已排队的提交；任务只捕获值，存储由其外层所有者排空/关闭。
    // 新会话通过持久回执恢复结果，不能重放内存候选或对旧 Pawn 补发副作用。
}
FAetherProfileSession FAetherProfileCoordinator::BeginSession(const FString& Character)
{
    check(IsInGameThread()&&!Impl->bPolling);
    if(!CharacterId(Character)||(!Impl->Sessions.Contains(Character)&&Impl->Sessions.Num()>=16))return {};
    FAetherProfileSession S{Character,FGuid::NewGuid(),1};Impl->Sessions.Add(Character,S);return S;
}
FAetherProfileSession FAetherProfileCoordinator::ReplacePawn(const FAetherProfileSession& S)
{
    check(IsInGameThread()&&!Impl->bPolling);
    if(!Impl->Current(S))return {};
    if(S.PawnEpoch==MAX_uint64){Impl->Sessions.Remove(S.CharacterId);return {};}
    auto Next=S;++Next.PawnEpoch;Impl->Sessions[S.CharacterId]=Next;return Next;
}
void FAetherProfileCoordinator::EndSession(const FAetherProfileSession& S)
{check(IsInGameThread()&&!Impl->bPolling);if(Impl->Current(S))Impl->Sessions.Remove(S.CharacterId);}
bool FAetherProfileCoordinator::IsCurrent(const FAetherProfileSession& S) const
{check(IsInGameThread());return Impl->Current(S);}
int32 FAetherProfileCoordinator::PendingCount() const
{check(IsInGameThread());return Impl->Jobs.Num();}
bool FAetherProfileCoordinator::Submit(const FAetherProfileSession& S,const FAetherPlayerCommand& C,FAetherCommandResult& Rejection)
{
    check(IsInGameThread()&&!Impl->bPolling);
    Rejection={};Rejection.CommandId=C.CommandId;
    const auto Fail=[&](EAetherCommandCode Why){Rejection.Code=Why;return false;};
    if(!Impl->Current(S))return Fail(EAetherCommandCode::Unauthorized);
    if(C.ProtocolVersion!=AetherCommands::ProtocolVersion)return Fail(EAetherCommandCode::UnsupportedProtocol);
    TArray<uint8> Request;FString Reason;if(!AetherCommands::Encode(C,Request,Reason))return Fail(EAetherCommandCode::Invalid);
    // 同一角色包括刚断开的旧会话最多一个在途命令；总量也有界，不能堆积大 Profile 副本。
    if(Impl->Jobs.Num()>=16||Impl->Jobs.ContainsByPredicate([&](const auto& J){return J->Session.CharacterId==S.CharacterId;}))
        return Fail(EAetherCommandCode::Busy);
    auto J=MakeUnique<FImpl::FJob>();J->Session=S;J->Command=C;J->Result.CommandId=C.CommandId;
    J->StoreFuture=Impl->Store->LookupReceipt({S.CharacterId,C.CommandId,C.ProtocolVersion,MoveTemp(Request)});
    Impl->Jobs.Add(MoveTemp(J));return true;
}
TArray<FAetherProfileCompletion> FAetherProfileCoordinator::Poll(const FAetherResolveProfileContext& Resolve)
{
    check(IsInGameThread()&&!Impl->bPolling);TGuardValue<bool> Guard(Impl->bPolling,true);
    TArray<FAetherProfileCompletion> Out;
    for(int32 I=Impl->Jobs.Num()-1;I>=0;--I)
    {
        auto& J=*Impl->Jobs[I];
        const auto Finish=[&](TOptional<FAetherProfileStateV10> Snapshot={})
        {
            FAetherProfileCompletion C;C.Session=J.Session;C.Result=J.Result;C.bMayPublish=Impl->Current(J.Session);
            if(C.bMayPublish)C.Snapshot=MoveTemp(Snapshot);
            Out.Add(MoveTemp(C));Impl->Jobs.RemoveAtSwap(I);
        };
        using E=FImpl::EStage;
        // 提交尚未发出时，换 Pawn/断线立即取消。提交已排队则等待其结束，但禁止旧 epoch 发布。
        if(!Impl->Current(J.Session)&&J.Stage!=E::Commit){J.Result.Code=EAetherCommandCode::Unauthorized;Finish();continue;}
        if(J.Stage==E::Receipt||J.Stage==E::Commit)
        {
            if(!J.StoreFuture.IsReady())continue;
            const auto R=J.StoreFuture.Get();
            if(!Impl->Current(J.Session)){J.Result.Code=EAetherCommandCode::Unauthorized;Finish();continue;}
            if(R.Code==EAetherStoreCode::Replayed||R.Code==EAetherStoreCode::Committed)
            {if(!Impl->Receipt(J,R))Finish();continue;}
            if(J.Stage==E::Receipt&&R.Code==EAetherStoreCode::Missing)
            {J.Stage=E::Read;J.ReadFuture=Impl->Store->Read({EAetherAggregateKind::Profile,J.Session.CharacterId});continue;}
            J.Result.Code=Code(R.Code);J.Result.FinalProfileRevision=R.FinalProfileRevision;Finish();continue;
        }
        if(!J.ReadFuture.IsReady())continue;
        const auto Read=J.ReadFuture.Get();FAetherProfileStateV10 Current;
        if(!Impl->Decode(Read,J.Session.CharacterId,Current))
        {J.Result.Code=Read.Code==EAetherStoreCode::Found?EAetherCommandCode::StorageUnavailable:Code(Read.Code);Finish();continue;}
        if(J.Stage==E::Refresh)
        {
            if(Current.Revision<J.Result.FinalProfileRevision)
            {J.Result.Code=EAetherCommandCode::StorageUnavailable;Finish();}
            else Finish(MoveTemp(Current));
            continue;
        }
        if(Current.Revision!=J.Command.ExpectedProfileRevision)
        {J.Result.Code=EAetherCommandCode::StaleRevision;J.Result.FinalProfileRevision=Current.Revision;Finish(MoveTemp(Current));continue;}
        FAetherProfileCommandContext Context;
        if(!Resolve||!Resolve(J.Session,Current,Context))
        {J.Result.Code=EAetherCommandCode::NotReady;J.Result.FinalProfileRevision=Current.Revision;Finish(MoveTemp(Current));continue;}
        FAetherTransaction Transaction;
        if(!AetherProfileCommands::Prepare(J.Command,J.Session.CharacterId,Current,Context,Impl->Items,Impl->Skills,Impl->Rules,Transaction,J.Result))
        {Finish(MoveTemp(Current));continue;}
        // 候选仅移交后台，直到持久提交成功并重读才可产生面向玩家的新快照。
        J.Stage=E::Commit;J.StoreFuture=Impl->Store->Commit(MoveTemp(Transaction));
    }
    return Out;
}
