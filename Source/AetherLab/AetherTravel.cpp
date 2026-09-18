#include "AetherFrontier.h"
#include "AetherAssetPreload.h"
#include "WorldPartition/WorldPartitionSubsystem.h"
#include "WorldPartition/WorldPartitionStreamingSource.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Components/CapsuleComponent.h"
#include "NavigationSystem.h"
#include "GameFramework/WorldSettings.h"
void AAetherFrontierCharacter::BeginSafeTravel(FVector Destination)
{
 if(!HasAuthority()||bTravelPending)return;
 ReleaseCarry();CancelActions();bTravelPending=true;TravelDestination=Destination;TravelOrigin=GetActorLocation();TravelStarted=CombatTime();
 SetBase(static_cast<UPrimitiveComponent*>(nullptr));GetCharacterMovement()->StopMovementImmediately();GetCharacterMovement()->DisableMovement();
 GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
 SetActorLocation(Destination,false,nullptr,ETeleportType::TeleportPhysics);if(auto* PC=Cast<APlayerController>(Controller))PC->ClientSetLocation(Destination,PC->GetControlRotation());ForceNetUpdate();
}
void AAetherFrontierCharacter::UpdateSafeTravel()
{
 if(!HasAuthority()||!bTravelPending)return;
 if(CombatTime()-TravelStarted>20){TravelDestination=TravelOrigin;TravelStarted=CombatTime();SetActorLocation(TravelOrigin,false,nullptr,ETeleportType::TeleportPhysics);Notify(TEXT("目的地尚未就绪，返回原位置。"));}
 auto* Assets=GetWorld()->GetSubsystem<UAetherAssetPreload>();if(!Assets||!Assets->Ready())return;
 auto* WP=GetWorld()->GetSubsystem<UWorldPartitionSubsystem>();
 if(WP&&!WP->IsStreamingCompleted())return;
 FHitResult Hit;FCollisionQueryParams Query(SCENE_QUERY_STAT(TravelReady),false,this);
 if(!GetWorld()->LineTraceSingleByChannel(Hit,TravelDestination+FVector(0,0,200),TravelDestination-FVector(0,0,500),ECC_Visibility,Query))return;
 if(GetWorld()->GetWorldSettings()->ActorHasTag("AetherNavBounds"))if(auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld())){FNavLocation Point;if(!Nav->ProjectPointToNavigation(TravelDestination,Point,FVector(300,300,500)))return;}
 SetBase(static_cast<UPrimitiveComponent*>(nullptr));SetActorLocation(TravelDestination,false,nullptr,ETeleportType::TeleportPhysics);if(auto* PC=Cast<APlayerController>(Controller))PC->ClientSetLocation(TravelDestination,PC->GetControlRotation());
 GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);GetCharacterMovement()->SetMovementMode(MOVE_Walking);bTravelPending=false;ForceNetUpdate();
}
