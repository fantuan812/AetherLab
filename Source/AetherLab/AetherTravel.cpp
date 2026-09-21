#include "AetherFrontier.h"
#include "AetherAssetPreload.h"
#include "Components/WorldPartitionStreamingSourceComponent.h"
#include "Components/SceneComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"

namespace
{
AActor* Source(UWorld* World,FVector Destination)
{
    FActorSpawnParameters Params;Params.ObjectFlags|=RF_Transient;
    AActor* Actor=World->SpawnActor<AActor>(Params);if(!Actor)return nullptr;
    auto* Root=NewObject<USceneComponent>(Actor);Actor->SetRootComponent(Root);Root->RegisterComponent();
    Actor->SetActorLocation(Destination);Actor->SetActorHiddenInGame(true);
    auto* Streaming=NewObject<UWorldPartitionStreamingSourceComponent>(Actor);
    Actor->AddInstanceComponent(Streaming);Streaming->TargetState=EStreamingSourceTargetState::Activated;
    Streaming->RegisterComponent();Streaming->EnableStreamingSource();return Actor;
}
bool Ready(AActor* SourceActor)
{
    const auto* S=SourceActor?SourceActor->FindComponentByClass<UWorldPartitionStreamingSourceComponent>():nullptr;
    return S&&S->IsStreamingCompleted();
}
bool Landing(AAetherFrontierCharacter& C,FVector Destination,FVector& Out)
{
    auto* Capsule=C.GetCapsuleComponent();auto* Movement=C.GetCharacterMovement();
    FCollisionQueryParams Query(SCENE_QUERY_STAT(AetherSafeTravel),false,&C);
    FHitResult Floor;
    if(!C.GetWorld()->LineTraceSingleByChannel(Floor,Destination+FVector(0,0,300),
        Destination-FVector(0,0,800),ECC_Pawn,Query)||!Movement->IsWalkable(Floor))return false;
    Out=Floor.ImpactPoint+FVector(0,0,Capsule->GetScaledCapsuleHalfHeight()+2);
    // 必须检查完整胶囊，地面命中本身不能证明头顶或侧面有净空。
    return !C.GetWorld()->OverlapBlockingTestByChannel(Out,C.GetActorQuat(),Capsule->GetCollisionObjectType(),
        FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(),Capsule->GetScaledCapsuleHalfHeight()),
        Query,FCollisionResponseParams(Capsule->GetCollisionResponseToChannels()));
}
}
void AAetherFrontierCharacter::ClearTravelSource()
{
    if(IsValid(TravelSourceActor))TravelSourceActor->Destroy();TravelSourceActor=nullptr;
}
void AAetherFrontierCharacter::BeginSafeTravel(FVector Destination)
{
    if(!HasAuthority()||bTravelPending||Destination.ContainsNaN())return;
    ClearTravelSource();TravelSourceActor=Source(GetWorld(),Destination);
    if(!TravelSourceActor){Notify(TEXT("无法建立目的地区域加载请求。"));return;}
    ReleaseCarry();CancelActions();CloseTrade();ReleaseHeldInput();
    bTravelPending=true;TravelToken=FGuid::NewGuid();TravelDestination=Destination;
    TravelOrigin=GetActorLocation();TravelStarted=CombatTime();bTravelClientReady=!IsPlayerControlled()||IsLocallyControlled();
    GetCharacterMovement()->StopMovementImmediately();GetCharacterMovement()->DisableMovement();
    // 仍停留在原地并保留碰撞，目标未就绪绝不先卸掉脚下地面。
    if(IsPlayerControlled()&&!IsLocallyControlled())ClientPrepareTravel(TravelToken,Destination);
    ForceNetUpdate();
}
void AAetherFrontierCharacter::ClientPrepareTravel_Implementation(FGuid Token,FVector Destination)
{
    if(HasAuthority()||!IsLocallyControlled())return;
    ClearTravelSource();TravelToken=Token;TravelDestination=Destination;TravelStarted=CombatTime();
    bTravelClientReady=false;bTravelCommitted=false;bTravelPending=true;ReleaseHeldInput();
    TravelSourceActor=Source(GetWorld(),Destination);
}
void AAetherFrontierCharacter::ServerTravelReady_Implementation(FGuid Token)
{
    // 客户端只能确认服务器已经指定的目的地，不能从 RPC 提供新位置。
    if(bTravelPending&&Token.IsValid()&&Token==TravelToken)bTravelClientReady=true;
}
void AAetherFrontierCharacter::ClientFinishTravel_Implementation(FGuid Token,bool Committed)
{
    if(HasAuthority()||Token!=TravelToken)return;
    bTravelPending=false;bTravelCommitted=Committed;
    if(!Committed){ClearTravelSource();TravelToken.Invalidate();}
    // 成功时保留预加载源，直到本 Pawn 的位置复制抵达，避免跨 Actor RPC 顺序造成短暂卸载。
}
void AAetherFrontierCharacter::UpdateSafeTravel()
{
    if(!HasAuthority())
    {
        if(!TravelToken.IsValid())return;
        if(bTravelCommitted&&(FVector::DistSquared(GetActorLocation(),TravelDestination)<FMath::Square(1200.)||CombatTime()-TravelStarted>30))
        {ClearTravelSource();TravelToken.Invalidate();return;}
        if(!bTravelPending||bTravelClientReady)return;
        auto* Assets=GetWorld()->GetSubsystem<UAetherAssetPreload>();FVector Spot;
        if(Assets&&Assets->Ready()&&Ready(TravelSourceActor)&&Landing(*this,TravelDestination,Spot))
        {bTravelClientReady=true;ServerTravelReady(TravelToken);}
        return;
    }
    if(!bTravelPending)return;
    const auto Finish=[&](bool Committed)
    {
        bTravelPending=false;SetBase(static_cast<UPrimitiveComponent*>(nullptr));
        GetCharacterMovement()->SetMovementMode(Alive()?MOVE_Falling:MOVE_None);
        if(IsPlayerControlled()&&!IsLocallyControlled())ClientFinishTravel(TravelToken,Committed);
        ClearTravelSource();TravelToken.Invalidate();ForceNetUpdate();
    };
    if(!Alive()||CombatTime()-TravelStarted>20)
    {Finish(false);Notify(TEXT("传送已取消，仍留在原位置。"));return;}
    auto* Assets=GetWorld()->GetSubsystem<UAetherAssetPreload>();
    auto* Mode=GetWorld()->GetAuthGameMode<AAetherFrontierMode>();FVector Spot;
    if(!bTravelClientReady||!Assets||!Assets->Ready()||!Ready(TravelSourceActor)||
       !Mode||!Mode->IsTravelRegionReady(TravelDestination)||!Landing(*this,TravelDestination,Spot))return;
    SetBase(static_cast<UPrimitiveComponent*>(nullptr));
    if(!TeleportTo(Spot,GetActorRotation(),false,false))return;
    if(auto* PC=Cast<APlayerController>(Controller))PC->ClientSetLocation(Spot,PC->GetControlRotation());
    Finish(true);
}
