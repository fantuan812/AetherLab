#include "World/AetherWorldCapability.h"
#include "ReactiveBodyComponent.h"
#include "EngineUtils.h"
UReactiveBodyComponent* AetherCapabilities::WaterReceiver(AActor* User,AActor* Container,double RangeCm)
{
 if(!IsValid(User)||!IsValid(Container)||User->GetWorld()!=Container->GetWorld())return nullptr;
 UReactiveBodyComponent* Best=nullptr;double Distance=FMath::Square(RangeCm);
 for(TActorIterator<AActor> It(User->GetWorld());It;++It)
 {
  auto* Capability=Cast<IAetherWorldCapability>(*It);if(*It==Container||!Capability||!Capability->HasWorldCapability("LiquidReceiver"))continue;
  auto* Body=Capability->ReactionBody();if(!Body||(Body->bOwnerOnlyStimuli&&It->GetOwner()!=User))continue;
  const double D=FVector::DistSquared(It->GetActorLocation(),Container->GetActorLocation());if(D>=Distance)continue;
  FCollisionQueryParams Q(SCENE_QUERY_STAT(CapabilityWater),false,Container);Q.AddIgnoredActor(*It);Q.AddIgnoredActor(User);
  if(User->GetWorld()->LineTraceTestByChannel(Container->GetActorLocation(),It->GetActorLocation(),ECC_Visibility,Q))continue;
  Best=Body;Distance=D;
 }
 return Best;
}
