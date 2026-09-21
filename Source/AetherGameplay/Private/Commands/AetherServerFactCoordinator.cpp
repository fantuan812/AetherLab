#include "Commands/AetherServerFactCoordinator.h"
#include "Definitions/AetherV10Definitions.h"
#include "Profile/AetherProfileCodec.h"
#include "World/AetherWorldCodec.h"
#include "Quests/AetherQuestProgression.h"
#include "Contracts/AetherPlayerCommand.h"
#include "Misc/DateTime.h"
#include "AetherServerReward.h"

namespace
{
bool Stable(const FString& S)
{
    if(S.IsEmpty()||S.Len()>128)return false;
    for(TCHAR C:S)if(!((C>='a'&&C<='z')||(C>='A'&&C<='Z')||(C>='0'&&C<='9')||C=='_'||C=='.'||C=='-'))return false;
    return true;
}
bool Same(const FAetherServerFact& A,const FAetherServerFact& B)
{return A.Kind!=EAetherServerFactKind::EquipmentWear&&A.Kind==B.Kind&&A.CharacterId.Equals(B.CharacterId,ESearchCase::CaseSensitive)&&A.FactId==B.FactId&&A.SourceId==B.SourceId&&A.UtcDay==B.UtcDay&&A.InstanceId==B.InstanceId;}
}
struct FAetherServerFactCoordinator::FImpl
{
    enum class EStage:uint8 {Queued,Read,Commit,Proof,Refresh};
    struct FJob
    {
        FAetherServerFact Event;
        EStage Stage=EStage::Queued;
        TFuture<FAetherStoreSnapshotResult> Read;
        TFuture<FAetherStoreResult> Write;
        TOptional<FAetherTransaction> Transaction;
        double RetryAt=0;
        int64 WearSequence=0; // 首次读取时分配，重试/冲突不能重新分配。
    };
    TSharedRef<IAetherTransactionalStore,ESPMode::ThreadSafe> Store;
    TArray<TUniquePtr<FJob>> Jobs;
    bool bPolling=false;
    explicit FImpl(TSharedRef<IAetherTransactionalStore,ESPMode::ThreadSafe> S):Store(MoveTemp(S)){}
    void Read(FJob& J,bool Refresh=false)
    {
        FAetherStoreSnapshotQuery Q;Q.Keys={{EAetherAggregateKind::Profile,J.Event.CharacterId},{EAetherAggregateKind::World,TEXT("Main")}};
        Q.bIncludeProfileRevisions=true;J.Read=Store->ReadSnapshot(MoveTemp(Q));J.Stage=Refresh?EStage::Refresh:EStage::Read;
    }
    void Proof(FJob& J)
    {
        const auto& T=J.Transaction.GetValue();FAetherReceiptQuery Q;
        Q.ActorId=T.ActorId;Q.CommandId=T.CommandId;Q.ProtocolVersion=T.ProtocolVersion;Q.Request=T.Request;
        J.Write=Store->LookupReceipt(MoveTemp(Q));J.Stage=EStage::Proof;
    }
};
FAetherServerFactCoordinator::FAetherServerFactCoordinator(TSharedRef<IAetherTransactionalStore,ESPMode::ThreadSafe> S)
    :Impl(MakeUnique<FImpl>(MoveTemp(S))){}
FAetherServerFactCoordinator::~FAetherServerFactCoordinator()=default;
int32 FAetherServerFactCoordinator::PendingCount() const{return Impl->Jobs.Num();}
bool FAetherServerFactCoordinator::HasPendingForCharacter(const FString& Id) const
{return Impl->Jobs.ContainsByPredicate([&](const auto& J){return J->Event.CharacterId.Equals(Id,ESearchCase::CaseSensitive);});}
bool FAetherServerFactCoordinator::HasPendingFact(const FString& Id,const FString& Fact) const
{return Impl->Jobs.ContainsByPredicate([&](const auto& J){return J->Event.CharacterId.Equals(Id,ESearchCase::CaseSensitive)&&J->Event.FactId.Equals(Fact,ESearchCase::CaseSensitive);});}
bool FAetherServerFactCoordinator::Enqueue(FAetherServerFact E,FString& Reason)
{
    check(IsInGameThread());const auto& D=FAetherV10Definitions::Get();
    bool Valid=!Impl->bPolling&&D.bValid&&!E.CharacterId.IsEmpty()&&E.CharacterId.Len()<=32;
    for(TCHAR C:E.CharacterId)Valid&=C>=32;
    FTCHARToUTF8 U(*E.CharacterId);FUTF8ToTCHAR Back(U.Get(),U.Length());Valid&=E.CharacterId.Equals(FString(Back.Length(),Back.Get()),ESearchCase::CaseSensitive);
    const bool Reward=E.Kind==EAetherServerFactKind::EncounterReward||E.Kind==EAetherServerFactKind::LegacyLoot;
    if(!Reward)Valid&=!E.InstanceId.IsValid();
    if(E.Kind!=EAetherServerFactKind::Daily&&E.Kind!=EAetherServerFactKind::EncounterReward)Valid&=E.UtcDay.IsEmpty();
    if(E.Kind!=EAetherServerFactKind::EquipmentWear)Valid&=E.WornItems.IsEmpty()&&E.WearCount==1;
    if(E.Kind==EAetherServerFactKind::EquipmentWear)
    {
        Valid&=E.WearCount>=1&&E.WearCount<=1000000&&E.FactId==TEXT("EquipmentWear")&&E.SourceId.IsEmpty()&&!E.WornItems.IsEmpty()&&E.WornItems.Num()<=10;
        TSet<FGuid> Unique;for(const auto& Id:E.WornItems){Valid&=Id.IsValid()&&!Unique.Contains(Id);Unique.Add(Id);}
    }
    else if(E.Kind==EAetherServerFactKind::EncounterReward)
    {
        if(E.UtcDay.IsEmpty())E.UtcDay=FDateTime::UtcNow().ToString(TEXT("%Y%m%d"));
        Valid&=(E.FactId==TEXT("Abbey")||E.FactId==TEXT("Relay"))&&E.SourceId.IsEmpty()&&E.InstanceId.IsValid();
    }
    else if(E.Kind==EAetherServerFactKind::LegacyLoot)
        Valid&=E.FactId==TEXT("Loot")&&E.SourceId.IsEmpty()&&E.InstanceId.IsValid();
    else if(E.Kind==EAetherServerFactKind::Daily)
    {
        if(E.UtcDay.IsEmpty())E.UtcDay=FDateTime::UtcNow().ToString(TEXT("%Y%m%d"));bool Known=false;
        for(const auto& Daily:D.Rules.Dailies)for(FName Fact:Daily.Facts)Known|=Fact.ToString().Equals(E.FactId,ESearchCase::CaseSensitive);
        Valid&=Known&&E.SourceId.IsEmpty();
    }
    else if(E.Kind==EAetherServerFactKind::Settle)Valid&=E.FactId.IsEmpty()&&E.SourceId.IsEmpty();
    else
    {
        const auto* Rule=Stable(E.FactId)?D.Rules.Objectives.Find(FName(*E.FactId)):nullptr;
        Valid&=Rule!=nullptr;
        if(E.Kind==EAetherServerFactKind::World)
        {
            Valid&=Rule&&Rule->Scope==EAetherObjectiveScope::World&&Stable(E.SourceId);
            bool Source=false;if(Rule)for(FName Id:Rule->FactSources)Source|=Id.ToString().Equals(E.SourceId,ESearchCase::CaseSensitive);
            Valid&=Source;
        }
        else Valid&=E.Kind==EAetherServerFactKind::Personal&&Rule&&Rule->Scope!=EAetherObjectiveScope::World&&E.SourceId.IsEmpty();
    }
    if(!E.UtcDay.IsEmpty())
    {
        Valid&=E.UtcDay.Len()==8&&E.UtcDay<=FDateTime::UtcNow().ToString(TEXT("%Y%m%d"));
        for(TCHAR C:E.UtcDay)Valid&=C>='0'&&C<='9';
        if(E.UtcDay.Len()==8)Valid&=FDateTime::Validate(FCString::Atoi(*E.UtcDay.Left(4)),FCString::Atoi(*E.UtcDay.Mid(4,2)),FCString::Atoi(*E.UtcDay.Right(2)),0,0,0,0);
    }
    if(!Valid){Reason=TEXT("Invalid trusted server fact or source");return false;}
    for(const auto& J:Impl->Jobs)if(Same(J->Event,E)){Reason.Reset();return true;}
    // 只压缩队尾尚未读取的同实例事件；已读、已提交或结果不确定的事务必须保持请求字节不变。
    if(E.Kind==EAetherServerFactKind::EquipmentWear&&!Impl->Jobs.IsEmpty()){
        auto& Last=*Impl->Jobs.Last();
        if(Last.Stage==FImpl::EStage::Queued&&!Last.Transaction.IsSet()&&Last.WearSequence==0&&
           Last.Event.Kind==E.Kind&&Last.Event.CharacterId==E.CharacterId&&Last.Event.WornItems==E.WornItems&&
           Last.Event.WearCount<=1000000-E.WearCount){Last.Event.WearCount+=E.WearCount;Reason.Reset();return true;}
    }
    if(Impl->Jobs.Num()>=128){Reason=TEXT("Server fact queue full; producer must retain unaccepted event");return false;}
    auto J=MakeUnique<FImpl::FJob>();J->Event=MoveTemp(E);Impl->Jobs.Add(MoveTemp(J));Reason.Reset();return true;
}
TArray<FAetherServerFactCompletion> FAetherServerFactCoordinator::Poll(double Now)
{
    check(IsInGameThread());TArray<FAetherServerFactCompletion> Done;
    if(Impl->bPolling||!FMath::IsFinite(Now)||Now<0)return Done;
    TGuardValue<bool> Guard(Impl->bPolling,true);const auto& D=FAetherV10Definitions::Get();
    using S=FImpl::EStage;TSet<FString> Active;
    for(int32 I=0;I<Impl->Jobs.Num();)
    {
        auto& J=*Impl->Jobs[I];
        if(Active.Contains(J.Event.CharacterId)){++I;continue;}
        if(Active.Num()>=16)break;Active.Add(J.Event.CharacterId);
        bool Finished=false;
        const auto Finish=[&](EAetherStoreCode Code,FString Why,TOptional<FAetherProfileStateV10> P={},TOptional<FAetherWorldStateV10> W={}){
            FAetherServerFactCompletion C;C.Event=J.Event;C.Code=Code;C.Detail=MoveTemp(Why);C.Profile=MoveTemp(P);C.World=MoveTemp(W);
            Done.Add(MoveTemp(C));Finished=true;
        };
        if(Now<J.RetryAt){++I;continue;}
        if(J.Stage==S::Queued)Impl->Read(J);
        else if(J.Stage==S::Commit||J.Stage==S::Proof)
        {
            if(J.Write.IsReady())
            {
                auto R=J.Write.Get();J.Write={};
                if(R.Code==EAetherStoreCode::Committed||R.Code==EAetherStoreCode::Replayed)Impl->Read(J,true);
                else if(R.Code==EAetherStoreCode::Conflict||R.Code==EAetherStoreCode::Missing||R.Code==EAetherStoreCode::Expired)
                {
                    // 集合事实、奖励 Claims 与持久磨损游标都可重读；磨损游标在 FJob 中保持不变。
                    J.Transaction.Reset();J.Stage=S::Queued;J.RetryAt=Now+.1;
                }
                else if(R.Code==EAetherStoreCode::Invalid||R.Code==EAetherStoreCode::Corrupt||R.Code==EAetherStoreCode::UnsupportedSchema)
                    Finish(R.Code,R.Detail);
                else {Impl->Proof(J);J.RetryAt=Now+1;} // 模糊存储错误仍查询原请求，不凭超时换 ID。
            }
        }
        else if(J.Read.IsReady())
        {
            auto R=J.Read.Get();J.Read={};
            if(R.Code!=EAetherStoreCode::Found)
            {
                if(R.Code==EAetherStoreCode::Busy||R.Code==EAetherStoreCode::Unavailable){Impl->Read(J,J.Stage==S::Refresh);J.RetryAt=Now+1;}
                else Finish(R.Code,R.Detail);
            }
            else
            {
                const auto* PR=R.Values.Find({EAetherAggregateKind::Profile,J.Event.CharacterId});
                const auto* WR=R.Values.Find({EAetherAggregateKind::World,TEXT("Main")});
                FAetherProfileStateV10 P;FAetherWorldStateV10 W;FString Why;
                if(!PR||!WR||PR->SchemaVersion!=10||WR->SchemaVersion!=10||
                    !AetherProfileCodec::Decode(PR->Payload,D.Items,D.Skills,D.Rules,P,Why)||P.Revision!=PR->Revision||
                    !P.CharacterId.Equals(J.Event.CharacterId,ESearchCase::CaseSensitive)||
                    !AetherWorldCodec::Decode(WR->Payload,D.Items,D.Rules,R.ProfileRevisions,W,Why)||W.Revision!=WR->Revision)
                    Finish(EAetherStoreCode::Corrupt,Why);
                else if(J.Stage==S::Refresh)Finish(EAetherStoreCode::Committed,{},MoveTemp(P),MoveTemp(W));
                else
                {
                    auto Next=P;auto World=W;
                    bool Allowed=true;
                    if(J.Event.Kind==EAetherServerFactKind::EquipmentWear)
                    {
                        if(!J.WearSequence&&Next.WearSequence<MAX_int64-1)J.WearSequence=Next.WearSequence+1;
                        Allowed=J.WearSequence>0;
                        if(Allowed&&Next.WearSequence<J.WearSequence)
                        {
                            Allowed=Next.WearSequence==J.WearSequence-1;
                            if(Allowed)
                            {
                                // 已售出/转移的旧实例不追索新装备；相同 GUID 仍在背包时照常结算。
                                for(const auto& Id:J.Event.WornItems)
                                    if(const auto* Item=Next.Inventory.Find(Id);Item&&Item->Durability>0)
                                        Allowed&=Next.Inventory.Wear(Id,J.Event.WearCount,D.Items).Code==EAetherInventoryMutationCode::Applied;
                                Next.WearSequence=J.WearSequence;
                            }
                        }
                    }
                    else if(J.Event.Kind==EAetherServerFactKind::EncounterReward||J.Event.Kind==EAetherServerFactKind::LegacyLoot)
                        Allowed=AetherServerRewards::Apply(J.Event,Next,World,D,Why);
                    else if(J.Event.Kind==EAetherServerFactKind::Personal)
                    {
                        Allowed=Next.Evidence.Contains(J.Event.FactId)||AetherQuestProgression::Observe(Next,J.Event.FactId,D.Rules);
                        if(Allowed&&J.Event.FactId==TEXT("Companion"))Next.bCompanion=true;
                    }
                    else if(J.Event.Kind==EAetherServerFactKind::World)
                    {
                        if(!World.WorldFactSources.Contains(J.Event.FactId))World.WorldFactSources.Add(J.Event.FactId,J.Event.SourceId);
                        // 实际参与者可记录非追溯世界事实；晚加入者只由 Settle 按 bRetroactive 规则补记。
                        if(!Next.Evidence.Contains(J.Event.FactId))AetherQuestProgression::Observe(Next,J.Event.FactId,D.Rules);
                    }
                    else if(J.Event.Kind==EAetherServerFactKind::Daily)
                    {
                        Allowed=false;
                        for(const auto& Daily:D.Rules.Dailies)if(Next.Claims.Contains(Daily.QuestGate.ToString()))
                            for(FName Fact:Daily.Facts)Allowed|=Fact.ToString().Equals(J.Event.FactId,ESearchCase::CaseSensitive);
                        // 过期观察不回退日历；同日重复事件保持集合幂等。
                        if(Allowed&&Next.DailyDate<=J.Event.UtcDay)
                        {
                            if(Next.DailyDate!=J.Event.UtcDay){Next.DailyDate=J.Event.UtcDay;Next.DailyEvidence.Reset();Next.DailyClaims.Reset();}
                            Next.DailyEvidence.AddUnique(J.Event.FactId);
                        }
                    }
                    const auto Settled=Allowed?AetherQuestProgression::Settle(Next,World.WorldFactSources,{},D.Items,D.Skills,D.Rules,D.Progression):FAetherQuestMutation();
                    TArray<uint8> ProfileBytes,WorldBytes;
                    if(!Allowed)Finish(EAetherStoreCode::Invalid,Why.IsEmpty()?TEXT("Fact is not available to this character"):Why);
                    else if((Settled.Code!=EAetherQuestMutationCode::Applied&&Settled.Code!=EAetherQuestMutationCode::Unchanged)||
                        !AetherProfileCodec::Encode(Next,D.Items,D.Skills,D.Rules,ProfileBytes,Why)||
                        !AetherWorldCodec::Encode(World,D.Items,D.Rules,R.ProfileRevisions,WorldBytes,Why))
                        Finish(EAetherStoreCode::Invalid,Why);
                    else if(ProfileBytes==PR->Payload&&WorldBytes==WR->Payload)
                        Finish(EAetherStoreCode::Replayed,{},MoveTemp(P),MoveTemp(W));
                    else if(P.Revision>=MAX_int64-1||W.Revision>=MAX_int64-1)
                        Finish(EAetherStoreCode::Invalid,TEXT("Server fact revision exhausted"));
                    else
                    {
                        ++Next.Revision;++World.Revision;auto Index=R.ProfileRevisions;Index[J.Event.CharacterId]=Next.Revision;
                        FAetherTransaction T;T.ActorId=J.Event.CharacterId;T.ExpectedProfileRevision=P.Revision;T.CommandId=AetherTransactions::NewCommandId(P.Revision);
                        FString Request=FString::Printf(TEXT("AETHER_SERVER_FACT_2|%d|%s|%s|%s|%s"),int32(J.Event.Kind),*J.Event.FactId,*J.Event.SourceId,*J.Event.UtcDay,*J.Event.InstanceId.ToString(EGuidFormats::Digits));
                        if(J.Event.Kind==EAetherServerFactKind::EquipmentWear)
                        {
                            Request+=FString::Printf(TEXT("|%lld|%d"),J.WearSequence,J.Event.WearCount);
                            for(const auto& Id:J.Event.WornItems)Request+=TEXT("|")+Id.ToString(EGuidFormats::Digits);
                        }
                        FTCHARToUTF8 Bytes(*Request);T.Request.Append(reinterpret_cast<const uint8*>(Bytes.Get()),Bytes.Length());
                        FAetherCommandResult Result;Result.CommandId=T.CommandId;Result.Code=EAetherCommandCode::Applied;
                        Result.FinalProfileRevision=Next.Revision;Result.FinalWorldRevision=World.Revision;
                        FAetherAggregateWrite PW,WW;PW.ExpectedRevision=P.Revision;WW.ExpectedRevision=W.Revision;
                        PW.Value.Key=PR->Key;WW.Value.Key=WR->Key;PW.Value.Revision=Next.Revision;WW.Value.Revision=World.Revision;
                        if(!AetherProfileCodec::Encode(Next,D.Items,D.Skills,D.Rules,PW.Value.Payload,Why)||
                            !AetherWorldCodec::Encode(World,D.Items,D.Rules,Index,WW.Value.Payload,Why)||!AetherCommands::EncodeResult(Result,T.Result,Why))
                            Finish(EAetherStoreCode::Invalid,Why);
                        else
                        {
                            T.Writes.Add(MoveTemp(PW));T.Writes.Add(MoveTemp(WW));
                            if(!AetherTransactions::Validate(T,Why))Finish(EAetherStoreCode::Invalid,Why);
                            else {J.Transaction=T;J.Write=Impl->Store->Commit(MoveTemp(T));J.Stage=S::Commit;}
                        }
                    }
                }
            }
        }
        if(Finished)Impl->Jobs.RemoveAt(I);else ++I;
    }
    return Done;
}
