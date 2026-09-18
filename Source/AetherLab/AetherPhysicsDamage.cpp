#include "AetherPhysicsDamage.h"
#include "AetherCombat.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"

bool FAetherImpactDamagePolicy::IsValid() const
{
    for(float V:{ThresholdJ,JoulesPerDamage,MaxDamage,PosturePerDamage,ReceiverCooldownSeconds})
        if(!FMath::IsFinite(V)||V<0)return false;
    return JoulesPerDamage>0&&MaxDamage<=10000&&PosturePerDamage<=100&&ReceiverCooldownSeconds<=10;
}
float FAetherImpactDamagePolicy::DamageFor(double EnergyJ) const
{
    return IsValid()&&FMath::IsFinite(EnergyJ)&&EnergyJ>ThresholdJ
        ? float(FMath::Clamp((EnergyJ-ThresholdJ)/JoulesPerDamage,0.,double(MaxDamage))) : 0.f;
}
void UAetherPhysicsDamageComponent::BeginPlay()
{
    Super::BeginPlay();
    if(GetOwner()->HasAuthority())
        if(auto* M=GetOwner()->FindComponentByClass<UReactiveMechanismComponent>())
        {Mechanism=M;ImpactDelegate=M->OnImpact.AddUObject(this,&UAetherPhysicsDamageComponent::ReceiveImpact);}
}
void UAetherPhysicsDamageComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    if(Mechanism.IsValid())Mechanism->OnImpact.Remove(ImpactDelegate);
    LastDamageAt.Reset();Super::EndPlay(Reason);
}
void UAetherPhysicsDamageComponent::ReceiveImpact(const FReactiveImpactEvent& Event)
{
    auto* Target=Event.Receiver.Get();
    if(!GetOwner()->HasAuthority()||!Mechanism.IsValid()||Event.Mechanism.Get()!=GetOwner()||!IsValid(Target)
        ||Target==GetOwner()||Target->GetWorld()!=GetWorld()||Event.EventId<=LastEventId||!FMath::IsFinite(Event.TimeSeconds))return;
    LastEventId=Event.EventId;
    const float Damage=Policy.DamageFor(Event.EnergyJ);if(Damage<=0)return;
    const double Now=GetWorld()->GetTimeSeconds();
    if(const double* Last=LastDamageAt.Find(Target);Last&&Now-*Last<Policy.ReceiverCooldownSeconds)return;
    // Claim before dispatch, since damage callbacks can synchronously emit another event.
    LastDamageAt.Add(Target,Now);
    for(auto It=LastDamageAt.CreateIterator();It;++It)if(!It.Key().IsValid()||Now-It.Value()>10)It.RemoveCurrent();
    AActor* Source=Event.Source.Get();
    auto* Pawn=Cast<APawn>(Source);auto* Controller=Pawn?Pawn->GetController():Source?Source->GetInstigatorController():nullptr;
    // Preserve the existing one accepted impact -> one material stimulus policy.
    // Chaos already resolved the collision; never apply the physical impulse a second time.
    if(auto* Body=Target->FindComponentByClass<UReactiveBodyComponent>())
    {FReactiveStimulus S;S.SourceActor=Source;S.bApplyPhysicsImpulse=false;S.ImpulseNs=Event.ImpulseNs;Body->Inject(S);}
    const float Applied=UGameplayStatics::ApplyDamage(Target,Damage,Controller,GetOwner(),nullptr);
    if(Applied>0&&IsValid(Target))if(auto* Character=Cast<AAetherCharacter>(Target))Character->ApplyPostureDamage(Applied*Policy.PosturePerDamage);
}
