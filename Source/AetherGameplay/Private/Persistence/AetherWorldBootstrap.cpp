#include "Persistence/AetherWorldBootstrap.h"
#include "Profile/AetherProfileCodec.h"
#include "World/AetherWorldCodec.h"
#include "World/AetherContainerCodec.h"

struct FAetherWorldBootstrap::FImpl
{
    TSharedRef<IAetherTransactionalStore,ESPMode::ThreadSafe> Store;
    EAetherBootstrapPhase Phase=EAetherBootstrapPhase::Idle;
    FString Failure;
    TOptional<FAetherLegacyImport> Legacy;
    bool AllowFresh=false;
    bool WroteInitial=false;
    TFuture<FAetherStoreSnapshotResult> Snapshot;
    TFuture<FAetherStoreResult> Write;
    TFuture<FAetherStoreReadResult> Read;
    FAetherStoreSnapshotResult Initial;
    TOptional<FAetherWorldStateV10> World;
    TArray<FString> Profiles,ContainerIds;
    TArray<FAetherContainerRestoreDescriptor> Containers;
    TSet<FGuid> Instances;
    int32 Cursor=0;
    explicit FImpl(TSharedRef<IAetherTransactionalStore,ESPMode::ThreadSafe> S):Store(S){}
    void Fail(const FString& Why)
    {
        Failure=Why.IsEmpty()?TEXT("Native startup read/validation failed"):Why;
        Phase=EAetherBootstrapPhase::Failed;World.Reset();Containers.Reset();Instances.Reset();Legacy.Reset();
        Snapshot={};Read={};Write={};
    }
    void ReadIndex(EAetherBootstrapPhase Next)
    {
        FAetherStoreSnapshotQuery Q;Q.Keys={{EAetherAggregateKind::World,TEXT("Main")}};
        Q.bIncludeProfileRevisions=true;Q.bIncludeContainerRevisions=true;Q.bIncludeContainerCount=true;Q.bIncludeWorldRevisions=true;
        Snapshot=Store->ReadSnapshot(MoveTemp(Q));Phase=Next;
    }
    bool Own(const FAetherInventoryStateV10& Inventory)
    {
        // 全库检查同一个物品 GUID 是否同时出现在角色和容器中，不能只校验每份数组内部。
        for(const auto& I:Inventory.Items){if(Instances.Contains(I.InstanceId))return false;Instances.Add(I.InstanceId);}
        return true;
    }
    void NextRow()
    {
        if(Phase==EAetherBootstrapPhase::Profiles&&Cursor>=Profiles.Num()){Phase=EAetherBootstrapPhase::Containers;Cursor=0;}
        if(Phase==EAetherBootstrapPhase::Containers&&Cursor>=ContainerIds.Num()){Instances.Reset();ReadIndex(EAetherBootstrapPhase::Rechecking);return;}
        const bool IsProfile=Phase==EAetherBootstrapPhase::Profiles;
        Read=Store->Read({IsProfile?EAetherAggregateKind::Profile:EAetherAggregateKind::Container,IsProfile?Profiles[Cursor]:ContainerIds[Cursor]});
    }
};
FAetherWorldBootstrap::FAetherWorldBootstrap(TSharedRef<IAetherTransactionalStore,ESPMode::ThreadSafe> S):Impl(MakeUnique<FImpl>(S)){}
FAetherWorldBootstrap::~FAetherWorldBootstrap()=default;
bool FAetherWorldBootstrap::Start(TOptional<FAetherLegacyImport> Legacy,bool AllowFresh,FString& Reason)
{
    check(IsInGameThread());
    const auto& D=FAetherV10Definitions::Get();
    if(Impl->Phase!=EAetherBootstrapPhase::Idle){Reason=TEXT("Bootstrap already started");return false;}
    if(!D.bValid){Reason=D.Error;Impl->Fail(Reason);return false;}
    if(Legacy.IsSet()&&!AetherImports::Validate(Legacy.GetValue(),Reason)){Impl->Fail(Reason);return false;}
    Impl->Legacy=MoveTemp(Legacy);Impl->AllowFresh=AllowFresh;Impl->ReadIndex(EAetherBootstrapPhase::Reading);Reason.Reset();return true;
}
void FAetherWorldBootstrap::Poll()
{
    check(IsInGameThread());auto& B=*Impl;const auto& D=FAetherV10Definitions::Get();FString Reason;
    if(B.Phase==EAetherBootstrapPhase::Reading||B.Phase==EAetherBootstrapPhase::Rechecking)
    {
        if(!B.Snapshot.IsValid()||!B.Snapshot.IsReady())return;
        auto S=B.Snapshot.Get();B.Snapshot={};
        if(S.Code!=EAetherStoreCode::Found||S.ContainerCount!=S.ContainerRevisions.Num()){B.Fail(S.Detail);return;}
        const auto* Row=S.Values.Find({EAetherAggregateKind::World,TEXT("Main")});
        if((Row&&S.WorldRevisions.Num()!=1)||(!Row&&!S.WorldRevisions.IsEmpty()))
        {B.Fail(TEXT("Unknown or duplicate world aggregate"));return;}
        if(Row&&(!S.WorldRevisions.Contains(TEXT("Main"))||S.WorldRevisions.FindChecked(TEXT("Main"))!=Row->Revision))
        {B.Fail(TEXT("World index differs from snapshot"));return;}
        if(B.Phase==EAetherBootstrapPhase::Rechecking)
        {
            const auto* Original=B.Initial.Values.Find({EAetherAggregateKind::World,TEXT("Main")});
            if(!Row||!Original||Row->Revision!=Original->Revision||Row->Payload!=Original->Payload||
                !S.ProfileRevisions.OrderIndependentCompareEqual(B.Initial.ProfileRevisions)||
                !S.ContainerRevisions.OrderIndependentCompareEqual(B.Initial.ContainerRevisions))
            {B.Fail(TEXT("Database changed during startup audit; stop other writers and retry"));return;}
            B.Phase=EAetherBootstrapPhase::Ready;B.Legacy.Reset();return;
        }
        if(!Row)
        {
            if(B.WroteInitial||!S.ProfileRevisions.IsEmpty()||!S.ContainerRevisions.IsEmpty())
            {B.Fail(TEXT("Missing world with existing aggregates; no blank fallback"));return;}
            if(B.Legacy.IsSet())B.Write=B.Store->ImportLegacy(B.Legacy.GetValue());
            else if(B.AllowFresh)
            {
                FAetherWorldStateV10 Empty;FAetherStoredAggregate Initial;Initial.Key={EAetherAggregateKind::World,TEXT("Main")};
                if(!AetherWorldCodec::Encode(Empty,D.Items,D.Rules,{},Initial.Payload,Reason)){B.Fail(Reason);return;}
                B.Write=B.Store->InitializeWorld(MoveTemp(Initial));
            }
            else {B.Fail(TEXT("World absent and no verified import or explicit fresh-world authorization"));return;}
            B.Phase=EAetherBootstrapPhase::Initializing;return;
        }
        FAetherWorldStateV10 World;
        if(Row->SchemaVersion!=10||!AetherWorldCodec::Decode(Row->Payload,D.Items,D.Rules,S.ProfileRevisions,World,Reason)||World.Revision!=Row->Revision)
        {B.Fail(Reason);return;}
        B.World=MoveTemp(World);B.Initial=MoveTemp(S);
        B.Initial.ProfileRevisions.GenerateKeyArray(B.Profiles);B.Initial.ContainerRevisions.GenerateKeyArray(B.ContainerIds);
        B.Profiles.Sort();B.ContainerIds.Sort();
        for(int32 I=1;I<B.Profiles.Num();++I)
            for(int32 J=0;J<I;++J)if(B.Profiles[I].Equals(B.Profiles[J],ESearchCase::IgnoreCase))
            {B.Fail(TEXT("Ambiguous character identity aliases"));return;}
        B.Cursor=0;B.Phase=EAetherBootstrapPhase::Profiles;B.NextRow();return;
    }
    if(B.Phase==EAetherBootstrapPhase::Initializing)
    {
        if(!B.Write.IsValid()||!B.Write.IsReady())return;
        const auto R=B.Write.Get();B.Write={};
        if(R.Code!=EAetherStoreCode::Committed&&R.Code!=EAetherStoreCode::Replayed){B.Fail(R.Detail);return;}
        // 提交成功并不等于可恢复；再次读库并审计全部 DTO，之后还要由场景适配器确认恢复。
        B.WroteInitial=true;B.ReadIndex(EAetherBootstrapPhase::Reading);return;
    }
    if(B.Phase!=EAetherBootstrapPhase::Profiles&&B.Phase!=EAetherBootstrapPhase::Containers)return;
    if(!B.Read.IsValid()||!B.Read.IsReady())return;
    const auto R=B.Read.Get();B.Read={};
    if(R.Code!=EAetherStoreCode::Found||!R.Value.IsSet()||R.Value->SchemaVersion!=10){B.Fail(R.Detail);return;}
    const auto& Row=R.Value.GetValue();
    if(B.Phase==EAetherBootstrapPhase::Profiles)
    {
        const auto& Id=B.Profiles[B.Cursor];FAetherProfileStateV10 P;
        if(Row.Revision!=B.Initial.ProfileRevisions.FindChecked(Id)||!AetherProfileCodec::Decode(Row.Payload,D.Items,D.Skills,D.Rules,P,Reason)||
            P.Revision!=Row.Revision||!P.CharacterId.Equals(Id,ESearchCase::CaseSensitive)||!B.Own(P.Inventory))
        {B.Fail(Reason.IsEmpty()?TEXT("Profile revision/identity/instance ownership invalid"):Reason);return;}
    }
    else
    {
        const auto& Id=B.ContainerIds[B.Cursor];FAetherContainerStateV10 C;
        if(Row.Revision!=B.Initial.ContainerRevisions.FindChecked(Id)||!AetherContainerCodec::Decode(Row.Payload,D.Items,C,Reason)||
            C.Revision!=Row.Revision||!C.ContainerId.Equals(Id,ESearchCase::CaseSensitive)||!B.Own(C.Inventory))
        {B.Fail(Reason.IsEmpty()?TEXT("Container revision/identity/instance ownership invalid"):Reason);return;}
        if(C.Kind==EAetherContainerKind::PersonalStorage&&!B.Profiles.ContainsByPredicate([&](const auto& P){return P.Equals(C.OwnerCharacterId,ESearchCase::CaseSensitive);}))
        {B.Fail(TEXT("Personal storage refers to a missing owner"));return;}
        B.Containers.Add({C.ContainerId,C.OwnerCharacterId,C.RegionId,C.Kind,C.Location,C.bActive,C.Revision});
    }
    ++B.Cursor;B.NextRow();
}
void FAetherWorldBootstrap::Stop()
{
    Impl->Snapshot={};Impl->Read={};Impl->Write={};Impl->World.Reset();Impl->Containers.Reset();Impl->Instances.Reset();Impl->Legacy.Reset();
    Impl->Phase=EAetherBootstrapPhase::Stopped;
}
EAetherBootstrapPhase FAetherWorldBootstrap::Phase() const{return Impl->Phase;}
const FString& FAetherWorldBootstrap::Failure() const{return Impl->Failure;}
const TOptional<FAetherWorldStateV10>& FAetherWorldBootstrap::World() const{return Impl->World;}
const TMap<FString,int64>& FAetherWorldBootstrap::ProfileRevisions() const{return Impl->Initial.ProfileRevisions;}
const TArray<FAetherContainerRestoreDescriptor>& FAetherWorldBootstrap::Containers() const{return Impl->Containers;}
bool AetherProfileBootstrap::BuildNew(const FString& Identity,FAetherStoredAggregate& Out,FString& Reason)
{
    const auto& D=FAetherV10Definitions::Get();if(!D.bValid){Reason=D.Error;return false;}
    FAetherProfileStateV10 P;P.CharacterId=Identity;P.Inventory.Capacity=D.Items.DefaultCapacity;
    if(P.Inventory.AddNew(TEXT("Potion"),2,D.Items).Code!=EAetherInventoryMutationCode::Applied)
    {Reason=TEXT("Starter item definition/capacity unavailable");return false;}
    FAetherStoredAggregate Row;Row.Key={EAetherAggregateKind::Profile,Identity};
    if(!AetherProfileCodec::Encode(P,D.Items,D.Skills,D.Rules,Row.Payload,Reason))return false;
    Out=MoveTemp(Row);return true;
}
