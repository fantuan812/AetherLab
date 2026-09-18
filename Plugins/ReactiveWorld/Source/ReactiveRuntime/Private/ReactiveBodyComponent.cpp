#include "ReactiveBodyComponent.h"
#include "ReactiveWorldSubsystem.h"
#include "ReactiveMechanismComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Abilities/GameplayAbilityTypes.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "Field/FieldSystemObjects.h"
#include "GameFramework/Actor.h"
#include "GeometryCollection/GeometryCollectionComponent.h"
#include "NativeGameplayTags.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Net/UnrealNetwork.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_ReactiveShock, "Event.Reactive.Shock");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_ReactiveIgnited, "Event.Reactive.Ignited");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_ReactiveFrozen, "Event.Reactive.Frozen");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_ReactiveBroken, "Event.Reactive.Broken");

UReactiveBodyComponent::UReactiveBodyComponent() { PrimaryComponentTick.bCanEverTick = false; SetIsReplicatedByDefault(true); }
void UReactiveBodyComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(UReactiveBodyComponent, State);DOREPLIFETIME(UReactiveBodyComponent,IceSupport);
    DOREPLIFETIME(UReactiveBodyComponent, bIceControlsPawnCollision);
}
void UReactiveBodyComponent::OnRep_State() { RefreshPresentation(); }
UPrimitiveComponent* UReactiveBodyComponent::GetPrimitive() const
{
    return GetOwner() ? GetOwner()->FindComponentByClass<UPrimitiveComponent>() : nullptr;
}
Reactive::FMaterial UReactiveBodyComponent::GetMaterial() const
{
    if (MaterialAsset) return MaterialAsset->Parameters.ToCore();
    switch (Preset)
    {
    case EReactiveMaterialPreset::Metal: return Reactive::FMaterial::Metal();
    case EReactiveMaterialPreset::Water: return Reactive::FMaterial::Water();
    case EReactiveMaterialPreset::Oil: return Reactive::FMaterial::Oil();
    case EReactiveMaterialPreset::Stone: return Reactive::FMaterial::Stone();
    default: return Reactive::FMaterial::Wood();
    }
}
void UReactiveBodyComponent::BeginPlay()
{
    Super::BeginPlay();
    if (UPrimitiveComponent* P = GetPrimitive()) { InitialCollision = P->GetCollisionEnabled(); InitialPawnResponse = P->GetCollisionResponseToChannel(ECC_Pawn); }
    if (bParticipatesInSimulation)
        if (UReactiveWorldSubsystem* World = GetWorld()->GetSubsystem<UReactiveWorldSubsystem>()) BodyId = World->RegisterBody(this);
}
void UReactiveBodyComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    if (UWorld* World = GetWorld()) if (auto* System = World->GetSubsystem<UReactiveWorldSubsystem>()) System->UnregisterBody(BodyId);
    BodyId = Reactive::InvalidBody;
    if (FireVisual) FireVisual->DestroyComponent();
    Super::EndPlay(Reason);
}
bool UReactiveBodyComponent::Inject(const FReactiveStimulus& Stimulus)
{
    if (auto* World = GetWorld()->GetSubsystem<UReactiveWorldSubsystem>()) return World->Submit(this, Stimulus);
    return false;
}
void UReactiveBodyComponent::AcceptState(const Reactive::FState& NewState)
{
    State = FReactiveSnapshot::FromCore(NewState);
    if (auto* World = GetWorld()->GetSubsystem<UReactiveWorldSubsystem>()) LastSourceActor = World->GetBodyOwner(NewState.LastSource);
    RefreshPresentation();
}
void UReactiveBodyComponent::RefreshPresentation()
{
    if (bIceControlsPawnCollision)
    {
        if (UPrimitiveComponent* P = GetPrimitive())
        {
            const double IceMassThreshold=FMath::Max(.2,P->Bounds.BoxExtent.X*P->Bounds.BoxExtent.Y*4/10000.*IceMassPerSquareMeter);
            const bool WasSolid=P->GetCollisionResponseToChannel(ECC_Pawn)==ECR_Block;
            if(GetOwner()->HasAuthority())
            {
                const bool EnoughIce=State.WaterKg*State.IceFraction>=IceMassThreshold&&!State.bBroken;
                bool Solid=EnoughIce&&State.IceFraction>=(WasSolid?.90:.98);
                bool Occupied=false;
                if(Solid&&!WasSolid)
                {
                    FCollisionQueryParams Q(SCENE_QUERY_STAT(IceOccupancy),false,GetOwner());
                    Occupied=GetWorld()->OverlapAnyTestByObjectType(P->Bounds.Origin,FQuat::Identity,FCollisionObjectQueryParams(ECC_Pawn),FCollisionShape::MakeBox(P->Bounds.BoxExtent*.95),Q);
                    Solid=!Occupied;
                }
                const auto Next=Solid?(State.IceFraction<.98?EReactiveIceSupport::Thawing:EReactiveIceSupport::Bearing):Occupied?EReactiveIceSupport::FreezePending:EReactiveIceSupport::Liquid;
                if(Next!=IceSupport){IceSupport=Next;GetOwner()->ForceNetUpdate();}
            }
            const bool Solid=IceSupport==EReactiveIceSupport::Bearing||IceSupport==EReactiveIceSupport::Thawing;
            P->SetCollisionEnabled(Solid?ECollisionEnabled::QueryAndPhysics:ECollisionEnabled::QueryOnly);
            P->SetCollisionResponseToChannel(ECC_Pawn,Solid?ECR_Block:ECR_Ignore);
            // Stop routing onto thawing ice immediately; physical support lasts only while actual ice mass permits it.
            P->SetCanEverAffectNavigation(IceSupport==EReactiveIceSupport::Bearing);

        }
    }
    if (State.bBurning && BurningEffect && !FireVisual && GetWorld()->GetNetMode() != NM_DedicatedServer)
        FireVisual = UNiagaraFunctionLibrary::SpawnSystemAttached(BurningEffect, GetOwner()->GetRootComponent(), NAME_None,
            FVector::ZeroVector, FRotator::ZeroRotator, EAttachLocation::KeepRelativeOffset, false);
    if (FireVisual)
    {
        FireVisual->SetVariableFloat(TEXT("User.TemperatureC"), float(State.TemperatureC));
        FireVisual->SetVariableFloat(TEXT("User.FuelKg"), float(State.FuelKg));
        FireVisual->SetVariableBool(TEXT("User.Burning"), State.bBurning);
        if (!State.bBurning) FireVisual->Deactivate();
        else if (!FireVisual->IsActive()) FireVisual->Activate();
    }
}
void UReactiveBodyComponent::AcceptEvent(const Reactive::FEvent& Event)
{
    LastReactionChain=Event.RootCauseId; LastReactionSequence=Event.Sequence;
    UPrimitiveComponent* P = GetPrimitive();
    if (P && bEnableChaosOnBreak && State.bBroken && !bIceControlsPawnCollision)
    {
        P->SetSimulatePhysics(true);
        if (auto* World = GetWorld()->GetSubsystem<UReactiveWorldSubsystem>()) World->TrackMovement(BodyId);
    }
    if (Event.Kind == Reactive::EEvent::Impulse && P && P->IsSimulatingPhysics())
    {
        if(auto* M=GetOwner()->FindComponentByClass<UReactiveMechanismComponent>())if(auto* World=GetWorld()->GetSubsystem<UReactiveWorldSubsystem>())M->RecordImpactSource(World->GetBodyOwner(Event.Source));
        P->AddImpulse(Event.Vector * 100.0); // SI kg.m/s -> UE kg.cm/s.
    }
    if (Event.Kind == Reactive::EEvent::Broken && bEnableChaosOnBreak)
    {
        if (auto* Collection = Cast<UGeometryCollectionComponent>(P))
        {
            UUniformScalar* Strain = NewObject<UUniformScalar>(this);
            Strain->SetUniformScalar(1.e6f);
            Collection->ApplyPhysicsField(true, EGeometryCollectionPhysicsTypeEnum::Chaos_ExternalClusterStrain, nullptr, Strain);
        }
    }
    if (Event.Kind == Reactive::EEvent::Ignited && BurningEffect && GetWorld()->GetNetMode() != NM_DedicatedServer)
    {
        if (!FireVisual) FireVisual = UNiagaraFunctionLibrary::SpawnSystemAttached(BurningEffect, GetOwner()->GetRootComponent(), NAME_None,
            FVector::ZeroVector, FRotator::ZeroRotator, EAttachLocation::KeepRelativeOffset, false);
        if (FireVisual) FireVisual->Activate();
    }
    FGameplayTag Tag;
    switch (Event.Kind)
    {
    case Reactive::EEvent::Shock: Tag = TAG_ReactiveShock; break;
    case Reactive::EEvent::Ignited: Tag = TAG_ReactiveIgnited; break;
    case Reactive::EEvent::Frozen: Tag = TAG_ReactiveFrozen; break;
    case Reactive::EEvent::Broken: Tag = TAG_ReactiveBroken; break;
    default: break;
    }
    if (Tag.IsValid() && UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(GetOwner()))
    {
        FGameplayEventData Payload; Payload.EventTag = Tag; Payload.Target = GetOwner(); Payload.EventMagnitude = float(Event.Magnitude);
        if (auto* World = GetWorld()->GetSubsystem<UReactiveWorldSubsystem>()) Payload.Instigator = World->GetBodyOwner(Event.Source);
        UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(GetOwner(), Tag, Payload);
    }
    UE_LOG(LogTemp, Verbose, TEXT("Reactive event #%llu body=%u kind=%d magnitude=%.3f"), Event.Sequence, BodyId, int32(Event.Kind), Event.Magnitude);
    OnReaction.Broadcast(static_cast<EReactiveReaction>(Event.Kind), Event.Magnitude, Event.Vector);
}

void UReactiveBodyComponent::AcceptElectricalWindow(const FReactiveElectricalWindow& Window)
{
    const double Tolerance=FMath::Max(1.e-6,FMath::Abs(Window.DeliveredJ)*1.e-10);
    if(!GetOwner()->HasAuthority()||Window.StepId<=LastElectricalStep||!FMath::IsFinite(Window.DurationSeconds)||Window.DurationSeconds<.001||Window.DurationSeconds>.1
        ||!FMath::IsFinite(Window.DeliveredJ)||!FMath::IsFinite(Window.HeatJ)||!FMath::IsFinite(Window.UsefulJ)
        ||Window.DeliveredJ<0||Window.HeatJ<0||Window.UsefulJ<0||!FMath::IsNearlyEqual(Window.DeliveredJ,Window.HeatJ+Window.UsefulJ,Tolerance))return;
    double Delivered=0,Heat=0,Useful=0;TSet<uint64> Reactions;
    for(const auto& E:Window.Contributions)
    {
        if(!E.ReactionId||Reactions.Contains(E.ReactionId)||E.StepId!=Window.StepId||E.ReceiverId!=Window.ReceiverId||E.DurationSeconds!=Window.DurationSeconds
            ||!FMath::IsFinite(E.DeliveredJ)||!FMath::IsFinite(E.HeatJ)||!FMath::IsFinite(E.UsefulJ)||E.DeliveredJ<0||E.HeatJ<0||E.UsefulJ<0
            ||!FMath::IsNearlyEqual(E.DeliveredJ,E.HeatJ+E.UsefulJ,Tolerance))return;
        Reactions.Add(E.ReactionId);Delivered+=E.DeliveredJ;Heat+=E.HeatJ;Useful+=E.UsefulJ;
    }
    if(!FMath::IsNearlyEqual(Delivered,Window.DeliveredJ,Tolerance)||!FMath::IsNearlyEqual(Heat,Window.HeatJ,Tolerance)||!FMath::IsNearlyEqual(Useful,Window.UsefulJ,Tolerance))return;
    LastElectricalStep=Window.StepId;OnElectricalWindow.Broadcast(Window);
}
void UReactiveBodyComponent::ResetElectricalWindow()
{LastElectricalStep=0;FReactiveElectricalWindow Empty;OnElectricalWindow.Broadcast(Empty);}
