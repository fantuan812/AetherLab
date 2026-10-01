#include "AI/AetherNpcMeleeDecision.h"
#include "AI/AetherNpcSkillDecision.h"
#include "Combat/AetherCombat.h"
#include "AetherEquipmentComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"

void FAetherNpcMeleeDecision::Reset(){Pending={};ProfileId.Reset();TelegraphUntil=0;bIssuing=false;}
EAetherNpcMeleeChoice FAetherNpcMeleeDecision::Choose(AAetherCharacter& C,AAetherCharacter& Target,const FVector& Position,const FAetherNpcMeleeProfile& Profile)
{
    if(bIssuing)return EAetherNpcMeleeChoice::Unavailable;
    if(!Profile.IsValid()||!FAetherNpcSkillDecision::CanTarget(C,Target)||!Target.HasActorBegunPlay()||Position.ContainsNaN()||
        !C.Equipment||!C.Ready()||!FMath::IsFinite(C.MaxHealth)||C.MaxHealth<=0)
    {Reset();return EAetherNpcMeleeChoice::Unavailable;}
    const FName AttackId(*Profile.AttackId);const auto* Item=C.Equipment->InSlot(TEXT("MainHand"));const auto* Attack=Item?Item->FindAttack(AttackId):nullptr;
    if(!Attack||!Attack->IsValid()||Profile.StartRangeMarginCm>=Attack->ReachCm||!C.Equipment->CanStartAttack(AttackId))
    {Reset();return EAetherNpcMeleeChoice::Unavailable;}
    if(FVector::DistSquared2D(C.GetActorLocation(),Position)>FMath::Square(Attack->ReachCm-Profile.StartRangeMarginCm))
    {Reset();return EAetherNpcMeleeChoice::Approach;}
    if(!Pending.RequestId.IsValid()||Pending.Target.Get()!=&Target||Pending.Controller.Get()!=C.GetController()||Pending.System.Get()!=C.AbilitySystem||
        Pending.Equipment.Get()!=C.Equipment||Pending.LoadoutRevision!=C.Equipment->LoadoutRevision||Pending.ItemId!=Item->ItemId||
        Pending.AttackId!=AttackId||!ProfileId.Equals(Profile.Id,ESearchCase::CaseSensitive))
    {
        Reset();double Delay=0;if(!Profile.TelegraphFor(FMath::Clamp(double(C.Health()/C.MaxHealth),0.,1.),Delay))return EAetherNpcMeleeChoice::Unavailable;
        Pending.RequestId=FGuid::NewGuid();Pending.Target=&Target;Pending.Controller=C.GetController();Pending.System=C.AbilitySystem;Pending.Equipment=C.Equipment;
        Pending.AttackId=AttackId;Pending.ItemId=Item->ItemId;Pending.LoadoutRevision=C.Equipment->LoadoutRevision;
        Pending.RangeMarginCm=Profile.StartRangeMarginCm;Pending.Motion=Profile.Motion;Pending.MotionSpeedCmPerSecond=Profile.MotionSpeedCmPerSecond;
        ProfileId=Profile.Id;TelegraphUntil=C.CombatTime()+Delay;
    }
    return C.CombatTime()>=TelegraphUntil?EAetherNpcMeleeChoice::Ready:EAetherNpcMeleeChoice::Telegraph;
}
bool FAetherNpcMeleeDecision::CaptureIssued(const AAetherCharacter& C,FName AttackId,FAetherNpcMeleeIntent& Out) const
{
    Out={};if(!bIssuing||Pending.AttackId!=AttackId||!ValidateIssued(C,Pending))return false;
    Out=Pending;return true;
}
bool FAetherNpcMeleeDecision::ValidateIssued(const AAetherCharacter& C,const FAetherNpcMeleeIntent& Intent) const
{
    auto* Target=Intent.Target.Get();const auto* E=C.Equipment.Get();
    if(!bIssuing||!Intent.RequestId.IsValid()||Pending.RequestId!=Intent.RequestId||!Target||!Target->HasActorBegunPlay()||
        !FAetherNpcSkillDecision::CanTarget(C,*Target)||Intent.Controller.Get()!=C.GetController()||Intent.System.Get()!=C.AbilitySystem||
        Intent.Equipment.Get()!=E||!E||Intent.LoadoutRevision!=E->LoadoutRevision||C.QueryAction(EAetherActionKind::Melee)!=EAetherActionDenial::None)return false;
    const auto* Item=E->InSlot(TEXT("MainHand"));const auto* Attack=Item?Item->FindAttack(Intent.AttackId):nullptr;
    if(!Item||Item->ItemId!=Intent.ItemId||!Attack||!Attack->IsValid()||Intent.RangeMarginCm>=Attack->ReachCm||
        FVector::DistSquared2D(C.GetActorLocation(),Target->GetActorLocation())>FMath::Square(Attack->ReachCm-Intent.RangeMarginCm))return false;
    if(Intent.Motion==EAetherNpcAttackMotion::ForwardDuringActive&&(!C.GetCharacterMovement()||!C.GetCharacterMovement()->IsMovingOnGround()))return false;
    FCollisionQueryParams Q(SCENE_QUERY_STAT(AetherNpcMeleeCommit),false,&C);Q.AddIgnoredActor(Target);
    return !C.GetWorld()->LineTraceTestByChannel(C.GetActorLocation(),Target->GetActorLocation(),ECC_Visibility,Q);
}
bool FAetherNpcMeleeDecision::TryExecute(AAetherCharacter& C)
{
    if(bIssuing||!Pending.RequestId.IsValid()||C.CombatTime()<TelegraphUntil)return false;
    const FGuid Request=Pending.RequestId;bIssuing=true;const bool Result=C.RequestMelee(Pending.AttackId);
    if(Pending.RequestId==Request)Reset(); // 旧调用不能抹掉回调中创建的新意图。
    return Result;
}
