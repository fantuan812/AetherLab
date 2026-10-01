#include "Characters/AetherCompanionComponent.h"
#include "Framework/AetherFrontier.h"
#include "Interaction/AetherNearbyRegistry.h"
#include "Interaction/AetherActions.h"
#include "AIController.h"
#include "Effects/AetherBuffRuntime.h"
#include "Skills/AetherSkillAbilityBinding.h"
#include "Skills/AetherSkillCooldownState.h"
#include "Skills/AetherNpcSkillDefinitions.h"
#include "Inventory/AetherResourceGate.h"

namespace AetherRelations
{
bool SameRecruitmentOwner(const AAetherFrontierCharacter& A,const AAetherFrontierCharacter& B)
{return (A.CompanionOwner?A.CompanionOwner.Get():&A)==(B.CompanionOwner?B.CompanionOwner.Get():&B);}
bool SameParty(const AAetherFrontierCharacter& A,const AAetherFrontierCharacter& B)
{
    const auto* PA=A.CompanionOwner?A.CompanionOwner->ProfileState():A.ProfileState();
    const auto* PB=B.CompanionOwner?B.CompanionOwner->ProfileState():B.ProfileState();
    return PA&&PB&&!PA->PartyLeader.IsEmpty()&&PA->PartyLeader.Equals(PB->PartyLeader,ESearchCase::CaseSensitive);
}
bool Hostile(const AAetherCharacter& A,const AAetherCharacter& B)
{return (A.Fighter==EAetherFighter::Player)!=(B.Fighter==EAetherFighter::Player);}
bool CanParticipateEncounter(const AAetherFrontierCharacter& A,const AAetherCharacter& Target)
{
    const auto* Enemy=Cast<AAetherFrontierCharacter>(&Target);
    if(!Enemy||Enemy->EncounterId.IsNone())return true;
    auto* Mode=A.GetWorld()->GetAuthGameMode<AAetherFrontierMode>();
    return Mode&&Mode->Encounters&&Mode->Encounters->Participates(const_cast<AAetherFrontierCharacter*>(&A),Enemy->EncounterId);
}
bool CanAssist(const AAetherFrontierCharacter& A,const AAetherFrontierCharacter& B)
{return A.Fighter==EAetherFighter::Player&&B.Fighter==EAetherFighter::Player&&SameRecruitmentOwner(A,B);}
}
UAetherCompanionComponent::UAetherCompanionComponent()
{PrimaryComponentTick.bCanEverTick=true;PrimaryComponentTick.bStartWithTickEnabled=false;}
bool UAetherCompanionComponent::BeginCompanionControl()
{
    auto* C=Cast<AAetherFrontierCharacter>(GetOwner());
    if(!C||!C->HasAuthority()||!IsValid(C->CompanionOwner)||C->CompanionOwner==C||
        C->CompanionId.IsNone()||C->Fighter!=EAetherFighter::Player||!Cast<AAIController>(C->GetController()))return false;
    if(bManaged&&RecruitmentOwner.Get()==C->CompanionOwner&&ManagedController.Get()==C->GetController())return true;
    EndCompanionControl();
    RecruitmentOwner=C->CompanionOwner;ManagedController=C->GetController();bManaged=true;++ControlGeneration;
    NextPerception=0;NextSupportSample=0;SupportDiagnostic.Reset();C->bFollowing=false;SetComponentTickEnabled(true);return true;
}
void UAetherCompanionComponent::ClearOwnedActions()
{
    auto* C=Cast<AAetherFrontierCharacter>(GetOwner());if(!C)return;
    CombatTarget.Reset();NextPerception=0;
    if(OwnedReviveTarget.IsValid()&&C->ReviveTarget==OwnedReviveTarget.Get())C->ReviveTarget=nullptr;
    OwnedReviveTarget.Reset();
    CancelOwnedSupport();
    if(bOwnsGuard){C->ServerBlock(false);bOwnsGuard=false;}
    C->bFollowing=false;
}
void UAetherCompanionComponent::EndCompanionControl()
{
    if(!bManaged)return;
    ClearOwnedActions();bManaged=false;++ControlGeneration;
    RecruitmentOwner.Reset();ManagedController.Reset();SetComponentTickEnabled(false);
}
void UAetherCompanionComponent::EndPlay(const EEndPlayReason::Type Reason)
{EndCompanionControl();Super::EndPlay(Reason);}
void UAetherCompanionComponent::TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* Tick)
{
    Super::TickComponent(Dt,Type,Tick);auto* C=Cast<AAetherFrontierCharacter>(GetOwner());
    if(!bManaged||!C||!C->HasAuthority())return;
    if(!IsValid(C->CompanionOwner)||RecruitmentOwner.Get()!=C->CompanionOwner||
        ManagedController.Get()!=C->GetController()||!Cast<AAIController>(C->GetController()))
    {EndCompanionControl();return;}
    if(!C->Alive()||C->CombatTime()<C->StunUntil||C->bCompanionHold||C->bTravelPending)
    {ClearOwnedActions();return;}
    if(SupportIntent&&!SupportIntent->bIssuing&&C->CastExecutionId!=SupportIntent->Execution)SupportIntent.Reset();
    if(SupportIntent&&!IsSupportIntentValid(*SupportIntent))CancelOwnedSupport();
    if(OwnedReviveTarget.IsValid()&&C->ReviveTarget!=OwnedReviveTarget.Get())OwnedReviveTarget.Reset();
    auto* Mode=GetWorld()->GetAuthGameMode<AAetherFrontierMode>();
    if(Mode&&Mode->Encounters&&Mode->Encounters->IsChanneling(C)){CancelOwnedSupport();return;}
    const float Now=C->CombatTime();auto* Owner=RecruitmentOwner.Get();
    if(!Owner->Alive())
    {
        CancelOwnedSupport();
        if(bOwnsGuard){C->ServerBlock(false);bOwnsGuard=false;}CombatTarget.Reset();
        if(FVector::DistSquared2D(C->GetActorLocation(),Owner->GetActorLocation())>FMath::Square(170.))C->AddMovementInput(C->SafeMoveDirection(Owner->GetActorLocation()));
        else if(!C->ReviveTarget&&AetherRelations::CanAssist(*C,*Owner))
        {C->ReviveTarget=Owner;C->ReviveStarted=Now;C->ReviveDamageSerial=C->CombatRuntime->DamageReceivedCount;if(C->AbilitySystem->TryActivateAbilityByClass(UAetherReviveAbility::StaticClass()))OwnedReviveTarget=Owner;else C->ReviveTarget=nullptr;}
        return;
    }
    auto* Nearby=GetWorld()->GetSubsystem<UAetherNearbyRegistry>();if(!Nearby)return;
    if(C->bHealer&&TrySupport(*C))return;
    if(SupportIntent)return; // 保持本次实际瞄准；前摇/恢复仍归GAS，不能写第二张AI冷却表。
    if(Now>=NextPerception)
    {
        NextPerception=Now+.15f;CombatTarget.Reset();double Best=FMath::Square(900.);
        for(const auto& Weak:Nearby->Nearby(C->GetActorLocation(),900.))if(auto* Enemy=Cast<AAetherCharacter>(Weak.Get()))
        {
            if(!Enemy->Alive()||!AetherRelations::Hostile(*C,*Enemy)||!AetherRelations::CanParticipateEncounter(*C,*Enemy))continue;
            const double Distance=FVector::DistSquared(C->GetActorLocation(),Enemy->GetActorLocation());if(Distance>=Best)continue;
            FCollisionQueryParams Q(SCENE_QUERY_STAT(CompanionEnemy),false,C);Q.AddIgnoredActor(Enemy);
            if(GetWorld()->LineTraceTestByChannel(C->GetActorLocation(),Enemy->GetActorLocation(),ECC_Visibility,Q))continue;
            Best=Distance;CombatTarget=Enemy;
        }
    }
    if(CombatTarget.IsValid()&&(!CombatTarget->Alive()||!AetherRelations::CanParticipateEncounter(*C,*CombatTarget.Get())))CombatTarget.Reset();
    auto* Target=CombatTarget.Get();const FVector D=(Target?Target->GetActorLocation():Owner->GetActorLocation())-C->GetActorLocation();
    if(C->bHealer)
    {
        if(bOwnsGuard){C->ServerBlock(false);bOwnsGuard=false;}
        if(Target&&D.Size2D()<400)C->AddMovementInput(C->SafeMoveDirection(C->GetActorLocation()-D.GetSafeNormal2D()*350));
        else if(FVector::DistSquared2D(C->GetActorLocation(),Owner->GetActorLocation())>FMath::Square(350.))C->AddMovementInput(C->SafeMoveDirection(Owner->GetActorLocation()));
        return;
    }
    if(!Target){if(D.Size2D()>400)C->bFollowing=true;else if(D.Size2D()<250)C->bFollowing=false;}
    if(Target?D.Size2D()>145:C->bFollowing)C->AddMovementInput(C->SafeMoveDirection(Target?Target->GetActorLocation():Owner->GetActorLocation()));
    if(Target)
    {
        C->SetActorRotation(D.Rotation());if(C->GetController())C->GetController()->SetControlRotation(D.Rotation());
        const bool Guard=D.Size2D()<300&&(Target->bWindingUp||Target->Equipment->IsBusy());
        if(Guard||bOwnsGuard)C->ServerBlock(Guard);bOwnsGuard=Guard;
        if(!Guard&&D.Size2D()<165&&Now>C->NextCompanionAction&&C->Ready()){C->PerformMelee(false);C->NextCompanionAction=Now+1;}
    }
    else if(bOwnsGuard){C->ServerBlock(false);bOwnsGuard=false;}
}


bool UAetherCompanionComponent::HasSupportControl(const AAetherFrontierCharacter& C) const
{
    return bManaged&&C.HasAuthority()&&C.Alive()&&C.bHealer&&C.SkillAuthority==EAetherSkillAuthority::Definition&&
        !C.GetPlayerState<AAetherPlayerState>()&&ManagedController.Get()==C.GetController()&&Cast<AAIController>(C.GetController())&&
        RecruitmentOwner.IsValid()&&RecruitmentOwner.Get()==C.CompanionOwner&&RecruitmentOwner->Alive()&&
        !C.bCompanionHold&&!C.bTravelPending&&C.CombatTime()>=C.StunUntil&&C.AbilitySystem&&C.AbilitySystem->GetOwnerActor()==&C&&C.AbilitySystem->GetAvatarActor()==&C;
}
bool UAetherCompanionComponent::ValidPatient(const AAetherFrontierCharacter& C,const AAetherFrontierCharacter& P,const FAetherCompanionSupportProfile& Policy) const
{
    FGuid Life;
    return IsValid(&P)&&!P.IsActorBeingDestroyed()&&P.HasActorBegunPlay()&&P.GetWorld()==C.GetWorld()&&P.Alive()&&
        AetherRelations::CanAssist(C,P)&&FMath::IsFinite(P.MaxHealth)&&P.MaxHealth>0&&P.Health()/P.MaxHealth<Policy.PatientHealthRatioBelow&&AetherSkillLives::Resolve(P,Life);
}
void UAetherCompanionComponent::SupportUnavailable(AAetherFrontierCharacter& C,const FString& Reason)
{
    if(SupportDiagnostic==Reason)return;SupportDiagnostic=Reason;
    UE_LOG(LogTemp,Warning,TEXT("AETHER_COMPANION_SUPPORT_UNAVAILABLE actor=%s reason=%s"),*C.GetName(),*Reason);
    C.Feedback=TEXT("同伴支持策略不可用：")+Reason;
}
const FAetherCompanionSupportProfile* UAetherCompanionComponent::SupportPolicy(AAetherFrontierCharacter& C)
{
    const auto& Definitions=FAetherCompanionSupportDefinitions::Get();const auto* P=Definitions.ForLoadout(C.SkillLoadoutId);
    FString Why;
    if(!P){SupportUnavailable(C,Definitions.bValid?TEXT("当前loadout缺少显式支持策略绑定。"):Definitions.Error);return nullptr;}
    if(!UAetherNearbyRegistry::IsSupportedRadius(P->PatientSearchRadiusCm))
    {SupportUnavailable(C,TEXT("候选感知半径超出NearbyRegistry正式查询合同。"));return nullptr;}
    if(!P->ValidateSkills(FAetherSkillDefinitionsV10::Get(),FAetherBuffDefinitions::Get(),Why))
    {SupportUnavailable(C,Why);return nullptr;}
    const auto* Loadout=FAetherNpcSkillDefinitions::Get().Find(C.SkillLoadoutId);
    for(const auto Purpose:{EAetherCompanionSupportPurpose::SelfHealing,EAetherCompanionSupportPurpose::FriendlyHealing,EAetherCompanionSupportPurpose::Cooling})
    {
        const auto& Choice=P->Skill(Purpose);if(!Choice.bEnabled)continue;
        const auto* Spec=C.AbilitySystem?AetherSkillBinding::Find(*C.AbilitySystem,Choice.SkillId):nullptr;
        if(!Loadout||!Loadout->InitialGrants.ContainsByPredicate([&](const auto& G){return G.SkillId.Equals(Choice.SkillId,ESearchCase::CaseSensitive);})||
            !Spec||!C.SkillUnlocked(Choice.SkillId)||!FAetherSkillDefinitionsV10::Get().Effect(Choice.SkillId,Spec->Level))
        {SupportUnavailable(C,TEXT("缺少支持策略要求的正式SkillId授权：")+Choice.SkillId);return nullptr;}
    }
    SupportDiagnostic.Reset();return P;
}
bool UAetherCompanionComponent::IsSupportIntentValid(const FSupportIntent& I) const
{
    const auto* C=Cast<AAetherFrontierCharacter>(GetOwner());const auto* P=I.Patient.Get();FGuid Life,TargetLife;
    const auto* Policy=FAetherCompanionSupportDefinitions::Get().ForLoadout(I.Loadout);
    if(!C||!P||!Policy||I.bCanceled||!HasSupportControl(*C)||I.Generation!=ControlGeneration||I.Controller.Get()!=C->GetController()||
        I.Recruiter.Get()!=C->CompanionOwner||I.System.Get()!=C->AbilitySystem||I.TargetSystem.Get()!=P->AbilitySystem||C->SkillLoadoutId!=I.Loadout||
        C->CombatRuntime->DamageReceivedCount!=I.DamageSerial||!ValidPatient(*C,*P,*Policy)||!AetherSkillLives::Resolve(*C,Life)||Life!=I.Life||
        !AetherSkillLives::Resolve(*P,TargetLife)||TargetLife!=I.TargetLife)return false;
    const auto& Choice=Policy->Skill(I.Purpose);const auto* Spec=I.System->FindAbilitySpecFromHandle(I.Spec);
    if(!Choice.bEnabled||Choice.SkillId!=I.SkillId||!Spec||Spec->Level!=I.Rank||AetherSkillBinding::Identify(*Spec)!=I.SkillId||!C->SkillUnlocked(I.SkillId))return false;
    if(I.Purpose==EAetherCompanionSupportPurpose::SelfHealing)return P==C;
    if(P==C)return false;
    return I.Purpose!=EAetherCompanionSupportPurpose::Cooling||(P->Reactive&&P->Reactive->State.TemperatureC>Policy->CoolingAboveTemperatureC);
}
void UAetherCompanionComponent::CancelOwnedSupport()
{
    auto* C=Cast<AAetherFrontierCharacter>(GetOwner());auto I=SupportIntent;if(!I)return;
    I->bCanceled=true;SupportIntent.Reset(); // 取消广播前释放成员，不能把回调中的新意图清掉。
    if(!C||!I->System.IsValid())return;
    const auto* Spec=I->System->FindAbilitySpecFromHandle(I->Spec);
    // 请求前已排空源/目标到期事件；同步零前摇付款时正式GA已公布这个真实ID。
    if(!I->Execution.IsValid()&&I->bIssuing&&Spec&&Spec->IsActive()&&C->CastExecutionId.IsValid())I->Execution=C->CastExecutionId;
    if(!I->Execution.IsValid())return;CanceledSupportExecution=I->Execution;
    if(C->AbilitySystem==I->System.Get()&&I->System->GetAvatarActor()==C&&C->CastExecutionId==I->Execution&&Spec&&Spec->IsActive())
        I->System->CancelAbilityHandle(I->Spec);
}
bool UAetherCompanionComponent::ValidateSkillCommit(const FAetherCastExecution& Cast,const AAetherCharacter* ActualTarget)
{
    if(Cast.ExecutionId==CanceledSupportExecution&&Cast.ExecutionId.IsValid())return false;
    auto I=SupportIntent;if(!I)return true;
    if(!I->Execution.IsValid()&&I->bIssuing&&I->SkillId==Cast.SkillId)I->Execution=Cast.ExecutionId;
    if(I->Execution!=Cast.ExecutionId)return true; // 未由本组件发出的玩家/其他行为保持正式能力语义。
    return IsSupportIntentValid(*I)&&ActualTarget==I->Patient.Get()&&Cast.SkillId==I->SkillId&&Cast.Rank==I->Rank&&Cast.LifeId==I->Life&&
        (I->Purpose!=EAetherCompanionSupportPurpose::FriendlyHealing||Cast.TargetLifeId==I->TargetLife);
}
bool UAetherCompanionComponent::TryIssueSupport(AAetherFrontierCharacter& C,AAetherFrontierCharacter& Patient,const FAetherCompanionSupportProfile& Policy,EAetherCompanionSupportPurpose Purpose)
{
    const auto& Choice=Policy.Skill(Purpose);
    if(SupportIntent||!Choice.bEnabled||!HasSupportControl(C)||!ValidPatient(C,Patient,Policy))return false;
    if((Purpose==EAetherCompanionSupportPurpose::SelfHealing)!=(&Patient==&C))return false;
    if(Purpose==EAetherCompanionSupportPurpose::Cooling&&(!Patient.Reactive||Patient.Reactive->State.TemperatureC<=Policy.CoolingAboveTemperatureC))return false;
    // 首次可重入预检查之前冻结本次决策身份，预检查结束再创建请求；不把控制变化后的身体当原请求。
    auto I=MakeShared<FSupportIntent>();I->SkillId=Choice.SkillId;I->Purpose=Purpose;I->Loadout=C.SkillLoadoutId;
    I->Patient=&Patient;I->Recruiter=C.CompanionOwner;I->Controller=C.GetController();I->System=C.AbilitySystem;I->TargetSystem=Patient.AbilitySystem;
    I->Generation=ControlGeneration;I->DamageSerial=C.CombatRuntime->DamageReceivedCount;
    if(!AetherSkillLives::Resolve(C,I->Life)||!AetherSkillLives::Resolve(Patient,I->TargetLife))return false;
    const auto* InitialSpec=AetherSkillBinding::Find(*C.AbilitySystem,Choice.SkillId);if(!InitialSpec)return false;I->Spec=InitialSpec->Handle;I->Rank=InitialSpec->Level;
    C.BuffRuntime->FlushDue();if(&Patient!=&C)Patient.BuffRuntime->FlushDue();
    if(!IsSupportIntentValid(*I)||C.BuffRuntime->HasDue()||Patient.BuffRuntime->HasDue()||C.ResourceGate->IsBlocked()||Patient.ResourceGate->IsBlocked()||
        C.QueryAction(EAetherActionKind::Spell)!=EAetherActionDenial::None||C.CastExecutionId.IsValid())return false;
    auto* Spec=I->System->FindAbilitySpecFromHandle(I->Spec);
    if(!Spec||!Spec->Ability||Spec->IsActive()||!I->System->AbilityActorInfo.IsValid()||
        !Spec->Ability->CanActivateAbility(I->Spec,I->System->AbilityActorInfo.Get(),nullptr,nullptr,nullptr))return false;
    if(&Patient!=&C)C.GetController()->SetControlRotation((Patient.GetActorLocation()-C.SkillAimOrigin()).Rotation());
    FHitResult Hit;FVector Origin,Direction;
    if(!C.FindSkillTarget(Choice.SkillId,I->Rank,Hit,Origin,Direction)||(&Patient!=&C&&Hit.GetActor()!=&Patient)||!IsSupportIntentValid(*I))return false;
    SupportIntent=I;I->bIssuing=true;const bool Activated=C.TrySkill(I->SkillId);I->bIssuing=false;
    if(SupportIntent==I&&!I->bCanceled)
    {
        if(Activated&&C.CastExecutionId.IsValid())I->Execution=C.CastExecutionId;
        else SupportIntent.Reset(); // 零前摇可能已同步提交或失败；不伪造成功/恢复/CD。
    }
    return Activated;
}
bool UAetherCompanionComponent::TrySupport(AAetherFrontierCharacter& C)
{
    if(!HasSupportControl(C)||SupportIntent||C.CombatTime()<NextSupportSample)return false;
    const auto* Policy=SupportPolicy(C);if(!Policy)return false;
    NextSupportSample=C.CombatTime()+Policy->SampleIntervalSeconds;
    if(C.QueryAction(EAetherActionKind::Spell)!=EAetherActionDenial::None)return false;
    auto* Nearby=GetWorld()->GetSubsystem<UAetherNearbyRegistry>();if(!Nearby)return false;
    TArray<AAetherFrontierCharacter*> Patients;
    if(Policy->SelfHealing.bEnabled&&ValidPatient(C,C,*Policy))Patients.Add(&C);
    for(const auto& Weak:Nearby->Nearby(C.GetActorLocation(),Policy->PatientSearchRadiusCm))
        if(auto* P=Cast<AAetherFrontierCharacter>(Weak.Get());P&&P!=&C&&ValidPatient(C,*P,*Policy))Patients.Add(P);
    Patients.Sort([](const auto& A,const auto& B){const double AR=A.Health()/A.MaxHealth,BR=B.Health()/B.MaxHealth;return AR==BR?A.GetPathName()<B.GetPathName():AR<BR;});
    for(auto* P:Patients)
    {
        if(Policy->CoolingPriority==EAetherCompanionCoolingPriority::BeforeHealing&&TryIssueSupport(C,*P,*Policy,EAetherCompanionSupportPurpose::Cooling))return true;
        const auto Healing=P==&C?EAetherCompanionSupportPurpose::SelfHealing:EAetherCompanionSupportPurpose::FriendlyHealing;
        if(TryIssueSupport(C,*P,*Policy,Healing))return true;
        if(Policy->CoolingPriority==EAetherCompanionCoolingPriority::AfterHealing&&TryIssueSupport(C,*P,*Policy,EAetherCompanionSupportPurpose::Cooling))return true;
    }
    return false;
}
