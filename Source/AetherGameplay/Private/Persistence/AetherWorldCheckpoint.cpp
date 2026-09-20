#include "Persistence/AetherWorldCheckpoint.h"
#include "Definitions/AetherV10Definitions.h"
#include "World/AetherWorldCodec.h"

FAetherWorldCheckpoint::FAetherWorldCheckpoint(TSharedRef<IAetherTransactionalStore,ESPMode::ThreadSafe> S):Store(MoveTemp(S)){}
bool FAetherWorldCheckpoint::Start(FAetherCaptureWorldCheckpoint In,FString& Reason)
{
    check(IsInGameThread());
    if(State!=EAetherWorldCheckpointPhase::Idle||!In||!FAetherV10Definitions::Get().bValid)
    {Reason=TEXT("World checkpoint requires valid definitions and a capture adapter");return false;}
    Capture=MoveTemp(In);ReadLatest();Reason.Reset();return true;
}
void FAetherWorldCheckpoint::ReadLatest()
{
    Candidate.Reset();Encoded.Reset();FAetherStoreSnapshotQuery Q;
    Q.Keys={{EAetherAggregateKind::World,TEXT("Main")}};Q.bIncludeProfileRevisions=true;
    Read=Store->ReadSnapshot(MoveTemp(Q));State=EAetherWorldCheckpointPhase::Reading;
}
void FAetherWorldCheckpoint::Fail(EAetherStoreCode Code,FString Why)
{
    Outcome.Code=Code;Outcome.Detail=MoveTemp(Why);Outcome.World.Reset();
    Capture={};Candidate.Reset();Encoded.Reset();State=EAetherWorldCheckpointPhase::Failed;
}
void FAetherWorldCheckpoint::Poll()
{
    check(IsInGameThread());if(bPolling)return;TGuardValue<bool> Guard(bPolling,true);
    if(State==EAetherWorldCheckpointPhase::Reading)
    {
        if(!Read.IsValid()||!Read.IsReady())return;
        auto R=Read.Get();Read={};const auto* Row=R.Values.Find({EAetherAggregateKind::World,TEXT("Main")});
        if(R.Code!=EAetherStoreCode::Found||!Row)
        {Fail(R.Code==EAetherStoreCode::Found?EAetherStoreCode::Missing:R.Code,R.Detail);return;}
        const auto& D=FAetherV10Definitions::Get();FAetherWorldStateV10 Previous,Next;FString Why;
        if(Row->SchemaVersion!=10||!AetherWorldCodec::Decode(Row->Payload,D.Items,D.Rules,R.ProfileRevisions,Previous,Why)||Previous.Revision!=Row->Revision)
        {Fail(EAetherStoreCode::Corrupt,Why);return;}
        if(Previous.Revision>=MAX_int64-1){Fail(EAetherStoreCode::Invalid,TEXT("World revision exhausted"));return;}
        // 局部复制回调，Stop 可在回调中失效该检查点；不得销毁正在执行的 TFunction。
        auto Adapter=Capture;const bool Captured=Adapter(Previous,Next,Why);
        if(State!=EAetherWorldCheckpointPhase::Reading)return;
        if(!Captured){Fail(EAetherStoreCode::Invalid,Why);return;}
        if(Next.Revision!=Previous.Revision){Fail(EAetherStoreCode::Invalid,TEXT("Capture must not allocate world revisions"));return;}
        Next.Revision=Previous.Revision+1;FAetherAggregateWrite W;
        W.ExpectedRevision=Previous.Revision;W.Value.Key={EAetherAggregateKind::World,TEXT("Main")};W.Value.Revision=Next.Revision;
        if(!AetherWorldCodec::Encode(Next,D.Items,D.Rules,R.ProfileRevisions,W.Value.Payload,Why))
        {Fail(EAetherStoreCode::Invalid,Why);return;}
        Candidate=MoveTemp(Next);Encoded=W;Write=Store->CompareExchangeWorld(MoveTemp(W));State=EAetherWorldCheckpointPhase::Writing;
    }
    if(State==EAetherWorldCheckpointPhase::Writing)
    {
        if(!Write.IsValid()||!Write.IsReady())return;
        auto R=Write.Get();Write={};
        if(R.Code==EAetherStoreCode::Conflict&&++Conflicts<=3)
        {
            // 重新读并重新采集；绝不把旧候选仅改 revision 后覆盖最新世界。
            ReadLatest();return;
        }
        if(R.Code!=EAetherStoreCode::Committed&&R.Code!=EAetherStoreCode::Replayed){Fail(R.Code,R.Detail);return;}
        if(!R.Value.IsSet()||!Encoded.IsSet()||!(R.Value->Key==Encoded->Value.Key)||R.Value->Revision!=Encoded->Value.Revision||
            R.Value->SchemaVersion!=Encoded->Value.SchemaVersion||R.Value->Payload!=Encoded->Value.Payload)
        {Fail(EAetherStoreCode::Corrupt,TEXT("Backend returned a different checkpoint"));return;}
        Outcome.Code=R.Code;Outcome.World=MoveTemp(Candidate);Capture={};Encoded.Reset();State=EAetherWorldCheckpointPhase::Complete;
    }
}
void FAetherWorldCheckpoint::Stop()
{
    check(IsInGameThread());Read={};Write={};Capture={};Candidate.Reset();Encoded.Reset();
    // 已入队的写不能取消；关闭方须排空 store。Stopped 绝不作为“未提交”证明。
    Outcome={};Outcome.Detail=TEXT("Checkpoint stopped; queued writes may have committed");State=EAetherWorldCheckpointPhase::Stopped;
}
