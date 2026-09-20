#include "ReactiveWorldSubsystem.h"
#include "ReactiveBodyComponent.h"

bool UReactiveWorldSubsystem::FreezeForPersistence(const TArray<FName>& Ids)
{
    check(IsInGameThread());TArray<FReactiveSaveRecord> Captured;
    if(!IsAuthority()||!Simulation||!Capture(Captured))return false;
    TMap<FName,UReactiveBodyComponent*> Bodies;
    for(const auto& Pair:Components)if(auto* B=Pair.Value.Get();B&&!B->StableId.IsNone())Bodies.Add(B->StableId,B);
    TSet<FName> Seen;
    for(FName Id:Ids)
        if(Id.IsNone()||Seen.Contains(Id)||FrozenRecords.Contains(Id)||!Bodies.Contains(Id)||
            !Captured.ContainsByPredicate([&](const auto& R){return R.StableId==Id;}))return false;
        else Seen.Add(Id);
    // 先完整验证，再一次性移出求解器；不删除 Actor，提交失败仍可恢复相同物理状态。
    for(FName Id:Ids)
    {
        auto* B=Bodies[Id];FrozenRecords.Add(Id,*Captured.FindByPredicate([&](const auto& R){return R.StableId==Id;}));
        FrozenBodies.Add(Id,B);UnregisterBody(B->BodyId);B->BodyId=Reactive::InvalidBody;B->ResetElectricalWindow();
    }
    return true;
}
bool UReactiveWorldSubsystem::ResumeFrozen(const TArray<FName>& Ids)
{
    check(IsInGameThread());if(!IsAuthority()||!Simulation||Simulation->HasPendingInputs())return false;
    TArray<FReactiveSaveRecord> Records;TArray<UReactiveBodyComponent*> Bodies;TSet<FName> Seen;
    for(FName Id:Ids)
    {
        auto* B=FrozenBodies.FindRef(Id).Get();const auto* R=FrozenRecords.Find(Id);
        if(Seen.Contains(Id)||!B||!R||B->BodyId!=Reactive::InvalidBody)return false;
        Seen.Add(Id);Bodies.Add(B);Records.Add(*R);
    }
    const auto Rollback=[&](){for(auto* B:Bodies){UnregisterBody(B->BodyId);B->BodyId=Reactive::InvalidBody;}};
    for(auto* B:Bodies){B->BodyId=RegisterBody(B);if(B->BodyId==Reactive::InvalidBody){Rollback();return false;}}
    // 所有默认注册值在本帧复制之前被精确持久值替换，恢复失败不开放半初始化实体。
    if(!Restore(Records,true)){Rollback();return false;}
    DiscardFrozen(Ids);return true;
}
void UReactiveWorldSubsystem::ApplyCommittedPower(FName Id,bool bEnabled)
{
    check(IsInGameThread());if(!IsAuthority())return;
    if(auto* Record=FrozenRecords.Find(Id);Record&&Record->bHasMechanism)Record->bSourceEnabled=bEnabled;
}
void UReactiveWorldSubsystem::DiscardFrozen(const TArray<FName>& Ids)
{check(IsInGameThread());for(FName Id:Ids){FrozenRecords.Remove(Id);FrozenBodies.Remove(Id);}}
