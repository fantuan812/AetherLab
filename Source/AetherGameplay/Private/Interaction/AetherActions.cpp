#include "Interaction/AetherActions.h"
#include "Framework/AetherFrontier.h"
#include "Abilities/Tasks/AbilityTask_WaitDelay.h"
#include "NativeGameplayTags.h"
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_MeleeWindup,"Aether.Action.Melee.Windup");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_MeleeActive,"Aether.Action.Melee.Active");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_MeleeRecovery,"Aether.Action.Melee.Recovery");

UAetherMeleeAbility::UAetherMeleeAbility()
{
 NetExecutionPolicy=EGameplayAbilityNetExecutionPolicy::ServerOnly;
 InstancingPolicy=EGameplayAbilityInstancingPolicy::InstancedPerActor;
 bRetriggerInstancedAbility=false;
}
bool UAetherMeleeAbility::CheckCost(FGameplayAbilitySpecHandle H,const FGameplayAbilityActorInfo* Info,FGameplayTagContainer* Tags) const
{
 const auto* C=Info?Cast<AAetherCharacter>(Info->AvatarActor.Get()):nullptr;
 const FName Id=GetAbilityLevel(H,Info)>1?TEXT("Heavy"):TEXT("Light");
 const auto* Item=C?C->Equipment->InSlot(TEXT("MainHand")):nullptr; const auto* D=Item?Item->FindAttack(Id):nullptr;
 return C&&C->HasAuthority()&&C->AbilitySystem==Info->AbilitySystemComponent.Get()&&C->Equipment->CanStartAttack(Id)
     &&D&&C->Stamina()>=D->StaminaCost&&Super::CheckCost(H,Info,Tags);
}
void UAetherMeleeAbility::ApplyCost(FGameplayAbilitySpecHandle,const FGameplayAbilityActorInfo* Info,FGameplayAbilityActivationInfo) const
{
 if (!bCostApplied&&Info&&Info->AbilitySystemComponent.IsValid())
 {
  bCostApplied=true;
  Info->AbilitySystemComponent->ApplyModToAttribute(UAetherAttributes::GetStaminaAttribute(),EGameplayModOp::Additive,-PreparedCost);
 }
}
void UAetherMeleeAbility::ActivateAbility(FGameplayAbilitySpecHandle H,const FGameplayAbilityActorInfo* Info,FGameplayAbilityActivationInfo A,const FGameplayEventData*)
{
 bEnding=false;bCostApplied=false;PreparedCost=0;ActiveSerial=0;
 auto* C=Info?Cast<AAetherCharacter>(Info->AvatarActor.Get()):nullptr;
 const FName Id=GetAbilityLevel(H,Info)>1?TEXT("Heavy"):TEXT("Light");
 if (!IsValid(C)||!CheckCost(H,Info)) {EndAbility(H,Info,A,true,true);return;}
 auto* E=C->Equipment.Get(); const auto* Item=E->InSlot(TEXT("MainHand"));
 const FName ItemId=Item->ItemId; const int32 Revision=E->LoadoutRevision;
 PreparedCost=Item->FindAttack(Id)->StaminaCost;
 ActiveEquipment=E;ActiveSystem=Info->AbilitySystemComponent;
 // A commit can run callbacks. Revalidate the same avatar/loadout before starting a window.
 const bool Committed=CommitAbility(H,Info,A);
 if (!Committed||!IsActive()||!IsValid(C)||!C->AbilitySystem||C->AbilitySystem!=ActiveSystem.Get()||C->AbilitySystem->GetAvatarActor()!=C)
 {
  if(bCostApplied&&ActiveSystem.IsValid()) {ActiveSystem->ApplyModToAttribute(UAetherAttributes::GetStaminaAttribute(),EGameplayModOp::Additive,PreparedCost);bCostApplied=false;}
  if(IsActive())EndAbility(H,Info,A,true,true);return;
 }
 FinishedDelegate=E->OnAttackFinished.AddUObject(this,&UAetherMeleeAbility::AttackFinished);
 PhaseDelegate=E->OnAttackPhaseChanged.AddUObject(this,&UAetherMeleeAbility::AttackPhaseChanged);
 ActiveSerial=E->Attack.Serial+1;if(ActiveSerial==0)++ActiveSerial;
 if (!E->BeginCommittedAttack(Id,ItemId,Revision))
 {
  if(bCostApplied) {ActiveSystem->ApplyModToAttribute(UAetherAttributes::GetStaminaAttribute(),EGameplayModOp::Additive,PreparedCost);bCostApplied=false;}
  EndAbility(H,Info,A,true,true);
 }
}
void UAetherMeleeAbility::ClearPhaseTag()
{
 if(PhaseTag.IsValid()&&ActiveSystem.IsValid())ActiveSystem->RemoveLooseGameplayTag(PhaseTag,1,EGameplayTagReplicationState::TagOnly);
 PhaseTag=FGameplayTag();
}
void UAetherMeleeAbility::AttackPhaseChanged(uint32 Serial,EAetherAttackPhase Phase)
{
 if(Serial!=ActiveSerial||bEnding)return;ClearPhaseTag();
 if(Phase==EAetherAttackPhase::Windup)PhaseTag=TAG_MeleeWindup;
 else if(Phase==EAetherAttackPhase::Active)PhaseTag=TAG_MeleeActive;
 else if(Phase==EAetherAttackPhase::Recovery)PhaseTag=TAG_MeleeRecovery;
 if(PhaseTag.IsValid()&&ActiveSystem.IsValid())ActiveSystem->AddLooseGameplayTag(PhaseTag,1,EGameplayTagReplicationState::TagOnly);
}
void UAetherMeleeAbility::AttackFinished(uint32 Serial,bool Cancelled)
{ if(Serial==ActiveSerial&&IsActive()&&!bEnding)EndAbility(CurrentSpecHandle,CurrentActorInfo,CurrentActivationInfo,true,Cancelled); }
void UAetherMeleeAbility::EndAbility(FGameplayAbilitySpecHandle H,const FGameplayAbilityActorInfo* Info,FGameplayAbilityActivationInfo A,bool Replicate,bool Cancelled)
{
 if(bEnding||!IsEndAbilityValid(H,Info))return;
 if(ScopeLockCount>0)
 {WaitingToExecute.Add(FPostLockDelegate::CreateUObject(this,&UAetherMeleeAbility::EndAbility,H,Info,A,Replicate,Cancelled));return;}
 bEnding=true;
 if(auto* E=ActiveEquipment.Get())
 {
  E->OnAttackFinished.Remove(FinishedDelegate);E->OnAttackPhaseChanged.Remove(PhaseDelegate);
  if(ActiveSerial&&E->Attack.Serial==ActiveSerial)E->CancelAttack();
 }
 ClearPhaseTag();ActiveEquipment.Reset();ActiveSerial=0;
 // Keep the cost receipt until ActivateAbility returns, including synchronous commit cancellation.
 Super::EndAbility(H,Info,A,Replicate,Cancelled);
}
void UAetherMeleeAbility::OnAvatarSet(const FGameplayAbilityActorInfo* Info,const FGameplayAbilitySpec& Spec)
{
 if(IsInstantiated()&&IsActive()&&ActiveEquipment.IsValid()&&(!Info||ActiveEquipment->GetOwner()!=Info->AvatarActor.Get()))
  EndAbility(CurrentSpecHandle,CurrentActorInfo,CurrentActivationInfo,true,true);
 Super::OnAvatarSet(Info,Spec);
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
 Reviver->ReviveStarted=Reviver->CombatTime();Reviver->ReviveDamageSerial=Reviver->CombatRuntime->DamageReceivedCount;
 auto* Wait=UAbilityTask_WaitDelay::WaitDelay(this,3);Wait->OnFinish.AddDynamic(this,&UAetherReviveAbility::FinishRevive);Wait->ReadyForActivation();
}
void UAetherReviveAbility::FinishRevive()
{
 bool Success=false;
 if(IsValid(RescueTarget)&&RescueTarget->RescueHolder==Reviver&&IsValid(Reviver)&&Reviver->ReviveTarget==RescueTarget&&Reviver->Alive()&&IsValid(Reviver->ReviveTarget)&&!Reviver->ReviveTarget->Alive()&&Reviver->CombatTime()-Reviver->ReviveStarted>=2.99f&&Reviver->CombatRuntime->DamageReceivedCount==Reviver->ReviveDamageSerial&&Reviver->CombatTime()>=Reviver->StunUntil&&FVector::DistSquared(Reviver->GetActorLocation(),Reviver->ReviveTarget->GetActorLocation())<=FMath::Square(220.))
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
