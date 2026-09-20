#include "AetherFrontier.h"
#include "Persistence/AetherNativePersistence.h"
#include "Networking/AetherCommandRuntime.h"
#include "Inventory/AetherResourceGate.h"
#include "Engine/GameInstance.h"

void AAetherFrontierMode::SettleNativeEncounter(FAetherEncounterRun& Run)
{
    if(!bNativeSceneReady||!NativeWorld.IsSet()||Run.Phase!=EAetherEncounterPhase::Succeeded)return;
    const auto& Stored=Run.Definition=="Abbey"?NativeWorld->Abbey:NativeWorld->Relay;
    if(Stored.Instance!=Run.Instance||Stored.Phase!=5)
    {
        auto* Persistence=GetGameInstance()->GetSubsystem<UAetherNativePersistence>();
        if(!Persistence->IsSavingWorld())Persistence->SaveLoadedPhysics();
        return; // 成功阶段先持久化，重启后才有可证明的参与者与奖励实例。
    }
    for(const auto& Id:Stored.Settled)Run.Settled.AddUnique(Id);
    auto* Runtime=GetGameInstance()->GetSubsystem<UAetherCommandRuntime>();
    for(const auto& Id:Run.Participants)if(!Run.Settled.Contains(Id)&&!Runtime->HasPendingServerFact(Id,Run.Definition))
    {
        FAetherServerFact E;E.CharacterId=Id;E.Kind=EAetherServerFactKind::EncounterReward;E.FactId=Run.Definition.ToString();E.InstanceId=Run.Instance;
        FString Why;if(!Runtime->ObserveServerFact(MoveTemp(E),Why))UE_LOG(LogTemp,Warning,TEXT("AETHER_NATIVE_ENCOUNTER_REWARD_DEFERRED %s"),*Why);
    }
}
bool AAetherFrontierMode::RecordNativeCampClear(FName Definition,FGuid Instance)
{
    if(!bNativeSceneReady||!Instance.IsValid()||!NativeWorld.IsSet())return false;
    if(const auto* Done=NativeWorld->CampReceipts.FindByPredicate([&](const auto& R){return R.Definition==Definition.ToString();});Done&&Done->Instance==Instance){NativeCampWrites.Remove(Definition);return true;}
    if(auto* Pending=NativeCampWrites.Find(Definition))
    {
        if(Pending->Instance!=Instance||!Pending->Write.IsValid()||!Pending->Write.IsReady())return false;
        auto Result=Pending->Write.Get();NativeCampWrites.Remove(Definition);
        if(Result.World.IsSet())PublishNativeWorld(Result.World.GetValue());
        return (Result.Code==EAetherStoreCode::Committed||Result.Code==EAetherStoreCode::Replayed)&&Result.World.IsSet()&&
            Result.World->CampReceipts.ContainsByPredicate([&](const auto& R){return R.Instance==Instance&&R.Definition==Definition.ToString();});
    }
    const auto* Rule=FAetherRules::Get().Encounters.Find(Definition);
    const auto* Table=Rule?FAetherRules::Get().LootTables.Find(Rule->LootTable):nullptr;
    auto* Persistence=GetGameInstance()->GetSubsystem<UAetherNativePersistence>();
    if(!Rule||Rule->RespawnSeconds<=0||!Table||Persistence->IsSavingWorld())return false;
    const FString Key=Definition.ToString();const FVector Location=Rule->Center+FVector(0,100,-85);
    const int64 Respawn=FDateTime::UtcNow().ToUnixTimestamp()+FMath::CeilToInt(Rule->RespawnSeconds);
    TMap<FString,int32> Items;for(const auto& Item:*Table)Items.Add(Item.Key.ToString(),Item.Value);
    FNativeCampWrite Pending;Pending.Instance=Instance;
    Pending.Write=Persistence->SaveWorldMutation([Key,Instance,Location,Respawn,Items](const auto&,auto& Next,FString& Reason)
    {
        if(Next.CampReceipts.ContainsByPredicate([&](const auto& R){return R.Instance==Instance&&R.Definition==Key;}))return true;
        Next.Loot.RemoveAll([](const auto& L){return !L.ClaimedBy.IsEmpty();});
        if(Next.Loot.Num()>=128||Next.Loot.ContainsByPredicate([&](const auto& L){return L.ClaimId==Instance;}))
        {Reason=TEXT("Camp loot capacity or identity conflict");return false;}
        FAetherWorldLootV10 Loot;Loot.ClaimId=Instance;Loot.Location=Location;Loot.Items=Items;Next.Loot.Add(MoveTemp(Loot));
        Next.CampReceipts.RemoveAll([&](const auto& R){return R.Definition==Key;});
        Next.CampReceipts.Add({Key,Instance,Respawn});return true;
    });
    NativeCampWrites.Add(Definition,MoveTemp(Pending));return false;
}
FString AAetherFrontierMode::ClaimNativeLegacyLoot(AAetherFrontierCharacter* C,FName Id)
{
    auto* PS=C?C->ProfileState():nullptr;auto* Actor=Prop(Id);
    if(!bNativeSceneReady||!NativeWorld.IsSet()||!PS||!C->Ready()||C->ResourceGate->IsBlocked()||C->bTravelPending||
        !Actor||Actor->Service!="Loot"||FVector::DistSquared(C->GetActorLocation(),Actor->GetActorLocation())>FMath::Square(250.))
        return TEXT("掉落已变化或不在可领取范围内。");
    FCollisionQueryParams Q(SCENE_QUERY_STAT(NativeLegacyLoot),false,C);Q.AddIgnoredActor(Actor);
    if(GetWorld()->LineTraceTestByChannel(C->GetActorLocation(),Actor->GetActorLocation(),ECC_Visibility,Q))return TEXT("领取通路被遮挡。");
    const auto* Loot=NativeWorld->Loot.FindByPredicate([&](const auto& L){return Id.ToString()==TEXT("Loot_")+L.ClaimId.ToString(EGuidFormats::Digits);});
    if(!Loot||!Loot->ClaimedBy.IsEmpty())return TEXT("掉落已领取。");
    FAetherServerFact E;E.Kind=EAetherServerFactKind::LegacyLoot;E.CharacterId=PS->Profile.CharacterId;E.FactId=TEXT("Loot");E.InstanceId=Loot->ClaimId;
    FString Why;
    return GetGameInstance()->GetSubsystem<UAetherCommandRuntime>()->ObserveServerFact(MoveTemp(E),Why)?
        TEXT("正在保存领取结果，物品以收到的库存快照为准。"):TEXT("领取暂不可用，请稍后重试。");
}
