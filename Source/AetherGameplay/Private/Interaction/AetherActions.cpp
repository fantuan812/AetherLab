#include "Interaction/AetherActions.h"
#include "Framework/AetherFrontier.h"
#include "Abilities/Tasks/AbilityTask_WaitDelay.h"
#include "Abilities/Tasks/AbilityTask_ApplyRootMotionConstantForce.h"
#include "NativeGameplayTags.h"
#include "Inventory/AetherResourceGate.h"
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_MeleeWindup,"Aether.Action.Melee.Windup");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_MeleeActive,"Aether.Action.Melee.Active");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_MeleeRecovery,"Aether.Action.Melee.Recovery");

struct FAetherMeleePayment
{
 TWeakObjectPtr<UAbilitySystemComponent> System;
 TWeakObjectPtr<AAetherCharacter> Avatar;
 float Cost=0;
 bool bPaid=false,bEquipmentStarted=false;
};
namespace
{
void RefundUnstartedMelee(const TSharedPtr<FAetherMeleePayment>& Payment)
{
 if(!Payment||!Payment->bPaid||Payment->bEquipmentStarted)return;
 Payment->bPaid=false; // 退款通知同样可重入；先结清本凭据，绝不通过GA新成员找收款人。
 auto* C=Payment->Avatar.Get();auto* S=Payment->System.Get();
 if(C&&S&&C->Alive()&&C->AbilitySystem==S&&S->GetAvatarActor()==C)
     S->ApplyModToAttribute(UAetherAttributes::GetStaminaAttribute(),EGameplayModOp::Additive,Payment->Cost);
}
}
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
 FAetherNpcMeleeIntent Request;
 if(C&&C->EnemyMeleeDecision.IsIssuing()&&!C->EnemyMeleeDecision.CaptureIssued(*C,Id,Request))return false;
 return C&&C->HasAuthority()&&C->QueryAction(EAetherActionKind::Melee)==EAetherActionDenial::None&&C->AbilitySystem==Info->AbilitySystemComponent.Get()&&C->Equipment->CanStartAttack(Id)
     &&D&&C->Stamina()>=D->StaminaCost&&Super::CheckCost(H,Info,Tags);
}
void UAetherMeleeAbility::ApplyCost(FGameplayAbilitySpecHandle,const FGameplayAbilityActorInfo* Info,FGameplayAbilityActivationInfo) const
{
 const auto Payment=PaymentExecution;
 if(Payment&&!Payment->bPaid&&Info&&Payment->System.IsValid()&&Info->AbilitySystemComponent.Get()==Payment->System.Get())
 {
  Payment->bPaid=true;
  Payment->System->ApplyModToAttribute(UAetherAttributes::GetStaminaAttribute(),EGameplayModOp::Additive,-Payment->Cost);
 }
}
void UAetherMeleeAbility::ActivateAbility(FGameplayAbilitySpecHandle H,const FGameplayAbilityActorInfo* Info,FGameplayAbilityActivationInfo A,const FGameplayEventData*)
{
 if(Generation==MAX_uint64){EndAbility(H,Info,A,true,true);return;}
 const uint64 ThisGeneration=++Generation;
 const auto SameGeneration=[&]{return Generation==ThisGeneration;};
 StopOwnedMotion(); // 旧任务先结束；新执行不继承旧RootMotionSource。
 if(!SameGeneration())return;
 bEnding=false;ActiveSerial=0;ActiveExecutionId.Invalidate();NpcIntent.Reset();bMotionStarted=false;
 const auto Payment=MakeShared<FAetherMeleePayment>();PaymentExecution=Payment;
 auto* C=Info?Cast<AAetherCharacter>(Info->AvatarActor.Get()):nullptr;
 const FName Id=GetAbilityLevel(H,Info)>1?TEXT("Heavy"):TEXT("Light");
 if (!IsValid(C)||!CheckCost(H,Info)) {if(SameGeneration()&&IsActive())EndAbility(H,Info,A,true,true);return;}
 if(!SameGeneration())return;
 if(C->EnemyMeleeDecision.IsIssuing())
 {
  FAetherNpcMeleeIntent Request;if(!C->EnemyMeleeDecision.CaptureIssued(*C,Id,Request)){EndAbility(H,Info,A,true,true);return;}
  NpcIntent=Request;
 }
 ActiveCharacter=C;MotionDirection=C->GetActorForwardVector().GetSafeNormal2D();
 auto* E=C->Equipment.Get(); const auto* Item=E->InSlot(TEXT("MainHand"));
 const FName ItemId=Item->ItemId; const int32 Revision=E->LoadoutRevision;
 Payment->Cost=Item->FindAttack(Id)->StaminaCost;Payment->System=Info->AbilitySystemComponent;Payment->Avatar=C;
 ActiveEquipment=E;ActiveSystem=Info->AbilitySystemComponent;const auto Request=NpcIntent;
 // 本次支付凭据保活到原栈退回；同步取消后再激活不会替换原owner/amount/paid事实。
 const bool Committed=CommitAbility(H,Info,A);
 if(!SameGeneration()){RefundUnstartedMelee(Payment);return;}
 if (!Committed||!IsActive()||!IsValid(C)||!IsValid(E)||C->Equipment!=E||!C->AbilitySystem||C->AbilitySystem!=Payment->System.Get()||C->AbilitySystem->GetAvatarActor()!=C||
     (Request.IsSet()&&!C->EnemyMeleeDecision.ValidateIssued(*C,Request.GetValue())))
 {
  RefundUnstartedMelee(Payment);
  if(SameGeneration()&&IsActive())EndAbility(H,Info,A,true,true);return;
 }
 FinishedDelegate=E->OnAttackFinished.AddUObject(this,&UAetherMeleeAbility::AttackFinished);
 PhaseDelegate=E->OnAttackPhaseChanged.AddUObject(this,&UAetherMeleeAbility::AttackPhaseChanged);
 ActiveSerial=E->Attack.Serial+1;if(ActiveSerial==0)++ActiveSerial;
 const bool Started=E->BeginCommittedAttack(Id,ItemId,Revision);
 Payment->bEquipmentStarted=Started; // 正式Windup接受后取消保留其成本，不误当未提交退款。
 if(!SameGeneration()){RefundUnstartedMelee(Payment);return;}
 if(!Started)
 {
  RefundUnstartedMelee(Payment);
  if(SameGeneration()&&IsActive())EndAbility(H,Info,A,true,true);
 }
 else if(!ActiveExecution().IsValid()&&IsActive())EndAbility(H,Info,A,true,true);
}
FGuid UAetherMeleeAbility::ActiveExecution() const
{
 const auto* C=ActiveCharacter.Get();const auto* E=ActiveEquipment.Get();const auto* S=ActiveSystem.Get();
 return IsActive()&&!bEnding&&C&&C->Alive()&&!C->IsActorBeingDestroyed()&&E&&S&&C->Equipment==E&&C->AbilitySystem==S&&S->GetAvatarActor()==C&&
     E->IsBusy()&&!E->Attack.bCancelled&&E->Attack.Serial==ActiveSerial&&ActiveExecutionId.IsValid()&&E->Attack.ExecutionId==ActiveExecutionId?ActiveExecutionId:FGuid();
}
void UAetherMeleeAbility::StopOwnedMotion()
{
 auto* Previous=OwnedMotion.Get();OwnedMotion=nullptr;if(Previous)Previous->EndTask();
}
void UAetherMeleeAbility::StartOwnedMotion(FGuid ExpectedExecution,uint32 ExpectedSerial)
{
 if(bMotionStarted||!NpcIntent.IsSet()||NpcIntent->Motion!=EAetherNpcAttackMotion::ForwardDuringActive||!ExpectedExecution.IsValid()||ActiveExecution()!=ExpectedExecution||ActiveSerial!=ExpectedSerial)return;
 auto* C=ActiveCharacter.Get();auto* E=ActiveEquipment.Get();
 if(E->Attack.Serial!=ExpectedSerial||E->Attack.ExecutionId!=ExpectedExecution||E->Attack.Phase!=EAetherAttackPhase::Active)return;
 const auto* D=E->CurrentAttack();
 if(!D||NpcIntent->Controller.Get()!=C->GetController()||NpcIntent->System.Get()!=ActiveSystem.Get()||MotionDirection.IsNearlyZero())return;
 // 相位通知可能跨过整个有效窗口；只推进该真实执行尚余的Active时段，不补迟到位移。
 const float Remaining=D->WindupSeconds+D->ActiveSeconds-(E->Clock()-E->Attack.StartedAt);
 if(!FMath::IsFinite(Remaining)||Remaining<=0)return;
 bMotionStarted=true;
 OwnedMotion=UAbilityTask_ApplyRootMotionConstantForce::ApplyRootMotionConstantForce(this,FName(*(TEXT("Aether.NpcMelee.")+ActiveExecutionId.ToString(EGuidFormats::Digits))),
     MotionDirection,float(NpcIntent->MotionSpeedCmPerSecond),Remaining,false,nullptr,
     ERootMotionFinishVelocityMode::MaintainLastRootMotionVelocity,FVector::ZeroVector,0,true);
 if(OwnedMotion)OwnedMotion->ReadyForActivation();
}
void UAetherMeleeAbility::ClearPhaseTag()
{
 const auto PreviousTag=PhaseTag;const auto PreviousSystem=ActiveSystem;PhaseTag=FGameplayTag();
 // Remove可同步结束旧能力并启动新执行；旧栈不能在广播后覆盖新PhaseTag成员。
 if(PreviousTag.IsValid()&&PreviousSystem.IsValid())PreviousSystem->RemoveLooseGameplayTag(PreviousTag,1,EGameplayTagReplicationState::TagOnly);
}
void UAetherMeleeAbility::AttackPhaseChanged(uint32 Serial,EAetherAttackPhase Phase)
{
 if(Serial!=ActiveSerial||bEnding||!ActiveEquipment.IsValid())return;
 const FGuid ExpectedExecution=ActiveEquipment->Attack.ExecutionId;
 if(Phase==EAetherAttackPhase::Windup)ActiveExecutionId=ExpectedExecution;
 ClearPhaseTag();
 const auto SamePhase=[&]()
 {return ExpectedExecution.IsValid()&&ActiveExecution()==ExpectedExecution&&ActiveSerial==Serial&&ActiveEquipment.IsValid()&&ActiveEquipment->Attack.Phase==Phase;};
 if(!SamePhase())return;
 if(Phase==EAetherAttackPhase::Windup)PhaseTag=TAG_MeleeWindup;
 else if(Phase==EAetherAttackPhase::Active)PhaseTag=TAG_MeleeActive;
 else if(Phase==EAetherAttackPhase::Recovery)PhaseTag=TAG_MeleeRecovery;
 if(PhaseTag.IsValid()&&ActiveSystem.IsValid())ActiveSystem->AddLooseGameplayTag(PhaseTag,1,EGameplayTagReplicationState::TagOnly);
 // Add同样是重入边界；原Active通知绝不为刚开始Windup的新执行启动位移。
 if(Phase==EAetherAttackPhase::Active&&SamePhase())StartOwnedMotion(ExpectedExecution,Serial);
}
void UAetherMeleeAbility::AttackFinished(uint32 Serial,bool Cancelled)
{ if(Serial==ActiveSerial&&IsActive()&&!bEnding)EndAbility(CurrentSpecHandle,CurrentActorInfo,CurrentActivationInfo,true,Cancelled); }
void UAetherMeleeAbility::EndAbility(FGameplayAbilitySpecHandle H,const FGameplayAbilityActorInfo* Info,FGameplayAbilityActivationInfo A,bool Replicate,bool Cancelled)
{
 if(bEnding||!IsEndAbilityValid(H,Info))return;
 if(ScopeLockCount>0)
 {WaitingToExecute.Add(FPostLockDelegate::CreateUObject(this,&UAetherMeleeAbility::EndAbility,H,Info,A,Replicate,Cancelled));return;}
 bEnding=true;StopOwnedMotion(); // 先撤旧source，再广播装备取消；不回写结束速度覆盖后来的控制。
 if(auto* E=ActiveEquipment.Get())
 {
  E->OnAttackFinished.Remove(FinishedDelegate);E->OnAttackPhaseChanged.Remove(PhaseDelegate);
  if(ActiveSerial&&E->Attack.Serial==ActiveSerial)E->CancelAttack();
 }
 ClearPhaseTag();ActiveEquipment.Reset();ActiveCharacter.Reset();ActiveSystem.Reset();ActiveSerial=0;ActiveExecutionId.Invalidate();NpcIntent.Reset();
 PaymentExecution.Reset(); // 原Activate/ApplyCost栈仍各自持有共享凭据；不能在Super后的新激活上清理。
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
 if(!Reviver||Reviver->QueryAction(EAetherActionKind::Revive)!=EAetherActionDenial::None||!Reviver->ReviveTarget||Reviver->ReviveTarget->Alive())
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
 if(!IsActive()||!IsValid(Reviver)||!CurrentActorInfo||CurrentActorInfo->AvatarActor.Get()!=Reviver||
    CurrentActorInfo->AbilitySystemComponent.Get()!=Reviver->AbilitySystem||Reviver->AbilitySystem->GetAvatarActor()!=Reviver||
    Reviver->ResourceGate->IsBlocked()||!IsValid(RescueTarget)||RescueTarget->ResourceGate->IsBlocked())
 {if(IsActive())EndAbility(CurrentSpecHandle,CurrentActorInfo,CurrentActivationInfo,true,true);return;}
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
 if(!IsEndAbilityValid(H,Info))return;
 if(ScopeLockCount>0){WaitingToExecute.Add(FPostLockDelegate::CreateUObject(this,&UAetherReviveAbility::EndAbility,H,Info,A,Replicate,Cancelled));return;}
 if(IsValid(RescueTarget)&&RescueTarget->RescueHolder==Reviver){RescueTarget->RescueHolder.Reset();RescueTarget->RescueLeaseUntil=0;}
 if(IsValid(Reviver)&&Reviver->ReviveTarget==RescueTarget)Reviver->ReviveTarget=nullptr;
 RescueTarget=nullptr;Reviver=nullptr;Super::EndAbility(H,Info,A,Replicate,Cancelled);
}
