#include "Framework/AetherFrontier.h"
#include "Framework/AetherPlayerController.h"
#include "World/AetherNativeContainer.h"
#include "Persistence/AetherNativePersistence.h"
#include "World/AetherContainerCodec.h"
#include "Definitions/AetherV10Definitions.h"
#include "Engine/GameInstance.h"
#include "HAL/PlatformTime.h"

void AAetherFrontierMode::PublishNativeContainer(const FAetherContainerStateV10& C)
{
    if(!NativeSceneReady())return;
    auto* Found=NativeContainers.Find(C.ContainerId);auto* A=Found?Found->Get():nullptr;
    if(IsValid(A)&&C.Revision<A->Revision)return;
    if(!IsValid(A)&&C.bActive)
    {
        FAetherContainerRestoreDescriptor D{C.ContainerId,C.OwnerCharacterId,C.RegionId,C.Kind,C.Location,true,C.Revision};
        A=GetWorld()->SpawnActor<AAetherNativeContainer>();
        if(!A||!A->Configure(D)){if(A)A->Destroy();FailNativeScene(TEXT("Cannot publish committed container"));return;}
        NativeContainers.Add(C.ContainerId,A);
    }
    if(!IsValid(A))return;
    if(A->bOnlyRelevantToOwner)for(auto It=GetWorld()->GetPlayerControllerIterator();It;++It)
        if(auto* PS=It->Get()->GetPlayerState<AAetherPlayerState>();PS&&PS->Profile.CharacterId.Equals(C.OwnerCharacterId,ESearchCase::CaseSensitive))A->SetOwner(It->Get());
    if(!A->Publish(C)){FailNativeScene(TEXT("Container scene identity differs from committed row"));return;}
    if(!C.bActive)NativeContainers.Remove(C.ContainerId);
}
void AAetherFrontierMode::TickNativeContainers()
{
    if(!NativeSceneReady())return;const double Now=FPlatformTime::Seconds();
    TArray<FString> Finished;
    for(auto& Pair:NativeContainerCreates)
    {
        auto& Job=Pair.Value;if(!Job.Future.IsReady())continue;
        auto R=Job.Future.Get();Finished.Add(Pair.Key);FAetherContainerStateV10 C;FString Why;
        const auto& E=Job.Expected;
        if(R.Code==EAetherStoreCode::Found&&R.Value.IsSet()&&R.Value->SchemaVersion==10&&
            AetherContainerCodec::Decode(R.Value->Payload,FAetherV10Definitions::Get().Items,C,Why)&&C.Revision==R.Value->Revision&&
            C.ContainerId.Equals(E.ContainerId,ESearchCase::CaseSensitive)&&C.Kind==E.Kind&&C.OwnerCharacterId.Equals(E.OwnerCharacterId,ESearchCase::CaseSensitive))
        {PublishNativeContainer(C);NativeContainerRetry.Remove(Pair.Key);}
        else if(R.Code==EAetherStoreCode::Busy||R.Code==EAetherStoreCode::Unavailable)NativeContainerRetry.Add(Pair.Key,Now+2);
        else {FailNativeScene(TEXT("Configured container conflicts with persistent data"));return;}
    }
    for(const auto& Id:Finished)NativeContainerCreates.Remove(Id);
    const auto Ensure=[&](FAetherContainerStateV10 C)
    {
        if(NativeContainers.Contains(C.ContainerId)||NativeContainerCreates.Contains(C.ContainerId)||NativeContainerCreates.Num()>=16||NativeContainerRetry.FindRef(C.ContainerId)>Now)return;
        FContainerCreation Job;Job.Expected=C;Job.Future=GetGameInstance()->GetSubsystem<UAetherNativePersistence>()->CreateEmptyContainer(MoveTemp(C));
        const FString Id=Job.Expected.ContainerId;NativeContainerCreates.Add(Id,MoveTemp(Job));
    };
    for(const auto& Definition:FAetherV10Definitions::Get().Containers.Containers)
    {
        const auto Create=[&](const FString& Character)
        {
            FAetherContainerStateV10 C;C.Kind=Definition.Kind;
            C.ContainerId=Definition.Id+Character;C.OwnerCharacterId=Character;C.RegionId=Definition.Region;
            C.Location=Definition.Location;C.Inventory.Capacity=Definition.Capacity;Ensure(MoveTemp(C));
        };
        if(Definition.Kind==EAetherContainerKind::SharedChest)Create({});
        else for(auto It=GetWorld()->GetPlayerControllerIterator();It;++It)
            if(auto* PS=It->Get()->GetPlayerState<AAetherPlayerState>();PS&&PS->GetNativeProfile())Create(PS->Profile.CharacterId);
    }
}
