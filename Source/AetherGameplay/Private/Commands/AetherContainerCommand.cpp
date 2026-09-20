#include "Commands/AetherContainerCommand.h"
#include "Profile/AetherProfileCodec.h"
#include "World/AetherWorldCodec.h"
#include "World/AetherContainerCodec.h"
namespace
{
bool StableId(const FString& S)
{
    if(S.IsEmpty()||S.Len()>96)return false;
    for(TCHAR C:S)if(!((C>='A'&&C<='Z')||(C>='a'&&C<='z')||(C>='0'&&C<='9')||C=='_'||C=='.'||C=='-'))return false;return true;
}
EAetherCommandCode MutationCode(EAetherInventoryMutationCode C)
{
    using E=EAetherInventoryMutationCode;using R=EAetherCommandCode;
    switch(C){case E::Applied:return R::Applied;case E::Capacity:return R::Capacity;case E::Missing:return R::Missing;
    case E::Invalid:return R::Invalid;default:return R::NotAllowed;}
}
}
bool AetherContainerCommands::Handles(EAetherCommandType T)
{return T==EAetherCommandType::DropItem||T==EAetherCommandType::PickUpItem||T==EAetherCommandType::TransferItem;}
EAetherCommandCode AetherContainerCommands::AuthorizeRead(const FAetherPlayerCommand& C,const FAetherProfileCommandContext& Context,FString& Key)
{
    using E=EAetherCommandType;using R=EAetherCommandCode;Key.Reset();const auto& A=Context.Container;
    if(!Handles(C.Type))return R::UnsupportedAction;
    if(C.Type!=E::DropItem&&C.ProtocolVersion<3)return R::UnsupportedProtocol;
    if(!Context.bCanManageInventory)return R::NotReady;
    if(!A.bAuthorized)return R::Unauthorized;
    if(!A.bTargetReady)return R::NotReady;
    if(!StableId(A.ContainerId))return R::Invalid;
    if(C.Type==E::DropItem)
    {
        if(!A.bValidDropLocation||!StableId(A.RegionId)||A.DropLocation.ContainsNaN()||A.DropLocation.GetAbsMax()>1.e8)return R::NotAllowed;
    }
    else
    {
        if(!C.TargetStableId.Equals(A.TargetStableId,ESearchCase::CaseSensitive))return R::OutOfReach;
        if(!A.bCanWithdraw&&C.Type==E::PickUpItem)return R::NotAllowed;
        if(C.Type==E::PickUpItem&&(!A.bInventoryPickup||!A.bSafeToStore))return R::NotAllowed;
        if(C.Type==E::TransferItem)
        {
            if(!A.bContainerSession||!C.ContainerId.Equals(A.ContainerId,ESearchCase::CaseSensitive))return R::Unauthorized;
            if(C.TransferDirection==EAetherTransferDirection::IntoContainer?!A.bCanDeposit:(!A.bCanWithdraw||!A.bSafeToStore))return R::NotAllowed;
        }
    }
    Key=A.ContainerId;return R::Applied;
}
bool AetherContainerCommands::Prepare(const FAetherPlayerCommand& C,const FString& Actor,
    const FAetherStoreSnapshotResult& Snapshot,const FAetherProfileCommandContext& Context,
    const FAetherV10ItemDefinitions& Items,const FAetherSkillDefinitionsV10& Skills,const FAetherRules& Rules,
    FAetherTransaction& Transaction,FAetherCommandResult& Result)
{
    using E=EAetherCommandType;using R=EAetherCommandCode;Result={};Result.CommandId=C.CommandId;
    int64 ProfileRevision=-1,WorldRevision=-1;
    const auto Fail=[&](R Code)
    {
        Result.Code=Code;Result.FinalProfileRevision=ProfileRevision;Result.FinalWorldRevision=WorldRevision;
        Result.ActualQuantity=0;Result.AffectedIds.Reset();Result.Transfers.Reset();Result.ReasonParameters.Reset();return false;
    };
    FString Reason,Key;if(!AetherCommands::Validate(C,Reason))return Fail(AetherCommands::IsSupportedProtocol(C.ProtocolVersion)?R::Invalid:R::UnsupportedProtocol);
    const auto Authorized=AuthorizeRead(C,Context,Key);if(Authorized!=R::Applied)return Fail(Authorized);
    if(Snapshot.Code!=EAetherStoreCode::Found)return Fail(R::StorageUnavailable);
    const auto* ProfileRow=Snapshot.Values.Find({EAetherAggregateKind::Profile,Actor});
    const auto* WorldRow=Snapshot.Values.Find({EAetherAggregateKind::World,TEXT("Main")});
    if(!ProfileRow||!WorldRow)return Fail(R::NotReady);
    FAetherProfileStateV10 P;FAetherWorldStateV10 World;
    if(ProfileRow->SchemaVersion!=10||WorldRow->SchemaVersion!=10||
        !AetherProfileCodec::Decode(ProfileRow->Payload,Items,Skills,Rules,P,Reason)||!P.CharacterId.Equals(Actor,ESearchCase::CaseSensitive)||P.Revision!=ProfileRow->Revision||
        !Snapshot.ProfileRevisions.Contains(Actor)||Snapshot.ProfileRevisions[Actor]!=P.Revision||
        !AetherWorldCodec::Decode(WorldRow->Payload,Items,Rules,Snapshot.ProfileRevisions,World,Reason)||World.Revision!=WorldRow->Revision)
        return Fail(R::StorageUnavailable);
    ProfileRevision=P.Revision;WorldRevision=World.Revision;
    if(C.ExpectedProfileRevision!=ProfileRevision||C.ExpectedWorldRevision!=WorldRevision)return Fail(R::StaleRevision);
    if(ProfileRevision>=MAX_int64-1||WorldRevision>=MAX_int64-1)return Fail(R::NotReady);
    const auto* Existing=Snapshot.Values.Find({EAetherAggregateKind::Container,Key});
    FAetherContainerStateV10 Container;int64 ContainerRevision=-1;
    if(Existing)
    {
        if(Existing->SchemaVersion!=10||!AetherContainerCodec::Decode(Existing->Payload,Items,Container,Reason)||
            !Container.ContainerId.Equals(Key,ESearchCase::CaseSensitive)||Container.Revision!=Existing->Revision)return Fail(R::StorageUnavailable);
        ContainerRevision=Container.Revision;if(ContainerRevision>=MAX_int64-1)return Fail(R::NotReady);
    }
    else if(C.Type!=E::DropItem)return Fail(R::Missing);
    if(C.Type!=E::DropItem&&C.ExpectedContainerRevision!=ContainerRevision)return Fail(R::StaleRevision);
    FAetherInventoryMutation Move;
    if(C.Type==E::DropItem)
    {
        // 只复用服务器选定的非活动掉落墓碑，不覆盖活动掉落或个人仓储；新容器还需全局数量预算。
        if(Existing&&(Container.Kind!=EAetherContainerKind::WorldDrop||Container.bActive||!Container.Inventory.Items.IsEmpty()))return Fail(R::Conflict);
        if(!Existing&&(Snapshot.ContainerCount<0||Snapshot.ContainerCount>=4096))return Fail(R::Capacity);
        const auto Removable=P.Inventory.CanRemove(C.ItemInstanceId,Actor,false,Items);
        if(Removable!=EAetherInventoryMutationCode::Applied)return Fail(MutationCode(Removable));
        if(!Existing){Container.ContainerId=Key;Container.Kind=EAetherContainerKind::WorldDrop;Container.Inventory.Capacity=1;}
        Container.Location=Context.Container.DropLocation;Container.RegionId=Context.Container.RegionId;Container.bActive=true;
        Move=P.Inventory.TransferTo(Container.Inventory,C.ItemInstanceId,C.Quantity,false,Items);
    }
    else
    {
        if(!Container.Allows(Actor))return Fail(R::Unauthorized);
        if(C.Type==E::PickUpItem&&Container.Kind!=EAetherContainerKind::WorldDrop)return Fail(R::NotAllowed);
        if(C.Type==E::TransferItem&&Container.Kind==EAetherContainerKind::WorldDrop)return Fail(R::NotAllowed);
        const bool Into=C.Type==E::TransferItem&&C.TransferDirection==EAetherTransferDirection::IntoContainer;
        if(Into)
        {
            const auto* Item=P.Inventory.Find(C.ItemInstanceId);if(!Item)return Fail(R::Missing);
            if(Item->bLocked)return Fail(R::NotAllowed);
            if(Container.Kind==EAetherContainerKind::PersonalStorage)
            {
                if(!Item->BoundToCharacter.IsEmpty()&&!Item->BoundToCharacter.Equals(Actor,ESearchCase::CaseSensitive))return Fail(R::NotAllowed);
            }
            else
            {
                const auto Removable=P.Inventory.CanRemove(C.ItemInstanceId,Actor,false,Items);
                if(Removable!=EAetherInventoryMutationCode::Applied)return Fail(MutationCode(Removable));
            }
            Move=P.Inventory.TransferTo(Container.Inventory,C.ItemInstanceId,C.Quantity,true,Items);
        }
        else Move=Container.Inventory.TransferTo(P.Inventory,C.ItemInstanceId,C.Quantity,true,Items);
        if(Container.Kind==EAetherContainerKind::WorldDrop&&Container.Inventory.Items.IsEmpty())Container.bActive=false;
    }
    if(Move.Code!=EAetherInventoryMutationCode::Applied)return Fail(MutationCode(Move.Code));
    ++P.Revision;++World.Revision;Container.Revision=ContainerRevision+1;
    Result.Code=R::Applied;Result.FinalProfileRevision=P.Revision;Result.FinalWorldRevision=World.Revision;
    Result.ActualQuantity=Move.ActualQuantity;Result.AffectedIds=Move.AffectedIds;
    for(const auto& M:Move.Transitions){Result.Transfers.Add({M.From,M.To,M.Quantity});Result.AffectedIds.AddUnique(M.From);Result.AffectedIds.AddUnique(M.To);}
    Result.ReasonParameters.Add(TEXT("ContainerId"),Key);Result.ReasonParameters.Add(TEXT("ContainerRevision"),LexToString(Container.Revision));
    FAetherTransaction T;T.ActorId=Actor;T.CommandId=C.CommandId;T.ExpectedProfileRevision=ProfileRevision;T.ProtocolVersion=C.ProtocolVersion;
    FAetherAggregateWrite W;W.ExpectedRevision=ProfileRevision;W.Value.Key={EAetherAggregateKind::Profile,Actor};W.Value.Revision=P.Revision;
    if(!AetherProfileCodec::Encode(P,Items,Skills,Rules,W.Value.Payload,Reason))return Fail(R::Invalid);T.Writes.Add(W);
    W.ExpectedRevision=WorldRevision;W.Value.Key={EAetherAggregateKind::World,TEXT("Main")};W.Value.Revision=World.Revision;
    if(!AetherWorldCodec::Encode(World,Items,Rules,Snapshot.ProfileRevisions,W.Value.Payload,Reason))return Fail(R::Invalid);T.Writes.Add(W);
    W.ExpectedRevision=ContainerRevision;W.Value.Key={EAetherAggregateKind::Container,Key};W.Value.Revision=Container.Revision;
    if(!AetherContainerCodec::Encode(Container,Items,W.Value.Payload,Reason))return Fail(R::Invalid);T.Writes.Add(W);
    if(!AetherCommands::Encode(C,T.Request,Reason)||!AetherCommands::EncodeResult(Result,T.Result,Reason)||!AetherTransactions::Validate(T,Reason))return Fail(R::Invalid);
    Transaction=MoveTemp(T);return true;
}
