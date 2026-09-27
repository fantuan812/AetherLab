#include "Characters/AetherCompanionComponent.h"
#include "Framework/AetherFrontier.h"
#include "Interaction/AetherNearbyRegistry.h"
#include "Interaction/AetherActions.h"

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
UAetherCompanionComponent::UAetherCompanionComponent(){PrimaryComponentTick.bCanEverTick=true;}
void UAetherCompanionComponent::TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* Tick)
{
    Super::TickComponent(Dt,Type,Tick);auto* C=Cast<AAetherFrontierCharacter>(GetOwner());
    if(!C||!C->HasAuthority())return;
    if(RecruitmentOwner.Get()!=C->CompanionOwner){RecruitmentOwner=C->CompanionOwner;CombatTarget.Reset();NextPerception=0;C->ReviveTarget=nullptr;C->CancelCompanionHeal();}
    if(!RecruitmentOwner.IsValid()||!C->Alive()||C->CombatTime()<C->StunUntil||C->bCompanionHold||C->bTravelPending)
    {CombatTarget.Reset();C->ReviveTarget=nullptr;C->CancelCompanionHeal();C->ServerBlock(false);return;}
    auto* Mode=GetWorld()->GetAuthGameMode<AAetherFrontierMode>();
    if(Mode&&Mode->Encounters&&Mode->Encounters->IsChanneling(C))return;
    const float Now=C->CombatTime();auto* Owner=RecruitmentOwner.Get();
    if(!Owner->Alive())
    {
        C->ServerBlock(false);CombatTarget.Reset();
        if(FVector::DistSquared2D(C->GetActorLocation(),Owner->GetActorLocation())>FMath::Square(170.))C->AddMovementInput(C->SafeMoveDirection(Owner->GetActorLocation()));
        else if(!C->ReviveTarget&&AetherRelations::CanAssist(*C,*Owner))
        {C->ReviveTarget=Owner;C->ReviveStarted=Now;C->ReviveDamageSerial=C->CombatRuntime->DamageReceivedCount;if(!C->AbilitySystem->TryActivateAbilityByClass(UAetherReviveAbility::StaticClass()))C->ReviveTarget=nullptr;}
        return;
    }
    auto* Nearby=GetWorld()->GetSubsystem<UAetherNearbyRegistry>();if(!Nearby)return;
    if(C->bHealer&&!C->bCompanionHealPending&&Now>C->NextCompanionAction&&C->Mana()>=15)
    {
        AAetherFrontierCharacter* Patient=nullptr;float Lowest=.65f;
        for(const auto& Weak:Nearby->Nearby(C->GetActorLocation(),600.))if(auto* P=Cast<AAetherFrontierCharacter>(Weak.Get()))
        {
            if(!P->Alive()||!AetherRelations::CanAssist(*C,*P))continue;
            const float Ratio=P->Health()/FMath::Max(1.f,P->MaxHealth);if(Ratio>=Lowest)continue;
            FCollisionQueryParams Q(SCENE_QUERY_STAT(CompanionPatient),false,C);Q.AddIgnoredActor(P);
            if(P!=C&&GetWorld()->LineTraceTestByChannel(C->GetActorLocation(),P->GetActorLocation(),ECC_Visibility,Q))continue;
            Lowest=Ratio;Patient=P;
        }
        // Unsuccessful perception also has a bounded cadence.
        C->NextCompanionAction=Now+.15f;
        if(Patient)
        {
            if(Patient!=C&&Patient->Reactive->State.TemperatureC>55&&C->WaterReserveKg>=.5f&&C->Ready()&&C->GetController())
            {C->GetController()->SetControlRotation((Patient->GetActorLocation()-C->GetActorLocation()-FVector(0,0,55)).Rotation());if(C->TrySkill(TEXT("Water.Draw"))){C->NextCompanionAction=Now+1;return;}}
            C->NextCompanionAction=Now+5;C->ExecuteCompanionHeal(Patient);
        }
    }
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
        C->ServerBlock(false);
        if(Target&&D.Size2D()<400)C->AddMovementInput(C->SafeMoveDirection(C->GetActorLocation()-D.GetSafeNormal2D()*350));
        else if(FVector::DistSquared2D(C->GetActorLocation(),Owner->GetActorLocation())>FMath::Square(350.))C->AddMovementInput(C->SafeMoveDirection(Owner->GetActorLocation()));
        return;
    }
    if(!Target){if(D.Size2D()>400)C->bFollowing=true;else if(D.Size2D()<250)C->bFollowing=false;}
    if(Target?D.Size2D()>145:C->bFollowing)C->AddMovementInput(C->SafeMoveDirection(Target?Target->GetActorLocation():Owner->GetActorLocation()));
    if(Target)
    {
        C->SetActorRotation(D.Rotation());if(C->GetController())C->GetController()->SetControlRotation(D.Rotation());
        const bool Guard=D.Size2D()<300&&(Target->bWindingUp||Target->Equipment->IsBusy());C->ServerBlock(Guard);
        if(!Guard&&D.Size2D()<165&&Now>C->NextCompanionAction&&C->Ready()){C->PerformMelee(false);C->NextCompanionAction=Now+1;}
    }
    else C->ServerBlock(false);
}
