#include "AetherActions.h"
#include "AetherFrontier.h"
#include "Abilities/Tasks/AbilityTask_WaitDelay.h"
UAetherMeleeAbility::UAetherMeleeAbility()
{NetExecutionPolicy=EGameplayAbilityNetExecutionPolicy::ServerOnly;InstancingPolicy=EGameplayAbilityInstancingPolicy::InstancedPerActor;}
void UAetherMeleeAbility::ActivateAbility(FGameplayAbilitySpecHandle H,const FGameplayAbilityActorInfo* Info,FGameplayAbilityActivationInfo A,const FGameplayEventData*)
{
 auto* C=Info?Cast<AAetherCharacter>(Info->AvatarActor.Get()):nullptr;
 const bool Success=C&&C->Ready()&&C->Equipment->StartAttack(GetAbilityLevel(H,Info)>1?TEXT("Heavy"):TEXT("Light"));
 EndAbility(H,Info,A,true,!Success);
}
UAetherReviveAbility::UAetherReviveAbility()
{NetExecutionPolicy=EGameplayAbilityNetExecutionPolicy::ServerOnly;InstancingPolicy=EGameplayAbilityInstancingPolicy::InstancedPerActor;}
void UAetherReviveAbility::ActivateAbility(FGameplayAbilitySpecHandle H,const FGameplayAbilityActorInfo* Info,FGameplayAbilityActivationInfo A,const FGameplayEventData*)
{
 Reviver=Info?Cast<AAetherFrontierCharacter>(Info->AvatarActor.Get()):nullptr;
 if(!Reviver||!Reviver->Alive()||!Reviver->ReviveTarget||Reviver->ReviveTarget->Alive())
 {EndAbility(H,Info,A,true,true);return;}
 RescueTarget=Reviver->ReviveTarget;
 if((RescueTarget->RescueHolder.IsValid()&&RescueTarget->RescueHolder!=Reviver&&RescueTarget->RescueLeaseUntil>Reviver->CombatTime())||FVector::DistSquared(Reviver->GetActorLocation(),RescueTarget->GetActorLocation())>FMath::Square(220.))
 {EndAbility(H,Info,A,true,true);return;}
 RescueTarget->RescueHolder=Reviver;RescueTarget->RescueLeaseUntil=Reviver->CombatTime()+4;
 Reviver->ReviveStarted=Reviver->CombatTime();Reviver->ReviveDamageSerial=Reviver->DamageReceivedCount;
 auto* Wait=UAbilityTask_WaitDelay::WaitDelay(this,3);Wait->OnFinish.AddDynamic(this,&UAetherReviveAbility::FinishRevive);Wait->ReadyForActivation();
}
void UAetherReviveAbility::FinishRevive()
{
 bool Success=false;
 if(IsValid(RescueTarget)&&RescueTarget->RescueHolder==Reviver&&IsValid(Reviver)&&Reviver->ReviveTarget==RescueTarget&&Reviver->Alive()&&IsValid(Reviver->ReviveTarget)&&!Reviver->ReviveTarget->Alive()&&Reviver->CombatTime()-Reviver->ReviveStarted>=2.99f&&Reviver->DamageReceivedCount==Reviver->ReviveDamageSerial&&Reviver->CombatTime()>=Reviver->StunUntil&&FVector::DistSquared(Reviver->GetActorLocation(),Reviver->ReviveTarget->GetActorLocation())<=FMath::Square(220.))
 {
  FCollisionQueryParams Q(SCENE_QUERY_STAT(Revive),false,Reviver);Q.AddIgnoredActor(Reviver->ReviveTarget);
  if(!GetWorld()->LineTraceTestByChannel(Reviver->GetActorLocation(),Reviver->ReviveTarget->GetActorLocation(),ECC_Visibility,Q))
  {Reviver->ReviveTarget->SetVitals(40,Reviver->ReviveTarget->Mana(),40);Reviver->ReviveTarget->ResetCombat();Success=true;}
 }
 if(Reviver)Reviver->ReviveTarget=nullptr;
 EndAbility(CurrentSpecHandle,CurrentActorInfo,CurrentActivationInfo,true,!Success);
}

void UAetherReviveAbility::EndAbility(FGameplayAbilitySpecHandle H,const FGameplayAbilityActorInfo* Info,FGameplayAbilityActivationInfo A,bool Replicate,bool Cancelled)
{
 if(IsValid(RescueTarget)&&RescueTarget->RescueHolder==Reviver){RescueTarget->RescueHolder.Reset();RescueTarget->RescueLeaseUntil=0;}
 if(IsValid(Reviver)&&Reviver->ReviveTarget==RescueTarget)Reviver->ReviveTarget=nullptr;
 RescueTarget=nullptr;Reviver=nullptr;Super::EndAbility(H,Info,A,Replicate,Cancelled);
}
