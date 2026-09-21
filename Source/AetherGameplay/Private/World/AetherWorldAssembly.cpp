#include "Framework/AetherFrontier.h"
#include "World/AetherWorldDefinition.h"
#include "Movement/AetherTraversal.h"
#include "ReactiveWorldSubsystem.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"
#include "Components/StaticMeshComponent.h"

void AAetherFrontierMode::ApplyObjectDefinition(AAetherFrontierProp* A,FName Definition)
{
 const auto* D=FAetherWorldDefinitions::Get().Definitions.Find(Definition);if(!D)return;
 const auto& Caps=D->Capabilities;
 if(Caps.Contains("Metal")){A->Reactive->bParticipatesInSimulation=true;A->Reactive->Preset=EReactiveMaterialPreset::Metal;A->Reactive->InteractionRadiusCm=220;A->Spec.bInteractiveMaterial=true;A->Spec.Color=FLinearColor(.65f,.7f,.8f);}
 if(Caps.Contains("PowerSource")){A->Reactive->ReceiverLoad=0;A->Mechanism->PowerW=D->PowerW;A->Mechanism->RemainingEnergyJ=D->EnergyJ;}
 if(Caps.Contains("PowerReceiver")){A->Reactive->ReceiverLoad=D->ReceiverLoad;A->Reactive->ReceiverCapacityJ=500;A->Reactive->ElectricalHeatFraction=.05;A->Reactive->bElectricalTerminal=true;}
 A->Traversal->bAuthoredBridge=Caps.Contains("Bridge");A->Mechanism->bBuoyant=Caps.Contains("Buoyant");A->Mechanism->bReportImpacts=Caps.Contains("Impacts");
 if(Caps.Contains("Reactive")){A->Reactive->bParticipatesInSimulation=true;A->Spec.bInteractiveMaterial=true;}
 A->bCarryable=Caps.Contains("Carry");A->Reactive->bTrackMovement=A->bCarryable||Caps.Contains("Moving");
 A->Capabilities=Caps.Array();
}
AAetherFrontierProp* AAetherFrontierMode::SpawnPlacement(const FAetherWorldPlacement& E)
{
 if(auto* Existing=Prop(E.Id))return Existing;
 auto* A=Make(E.Id,E.Service,E.Location,E.Scale,EAetherObjectKind(E.Kind),E.Label);if(!A)return nullptr;
 A->Reactive->bAllowAbsentFromOlderSave=E.bAllowAbsentFromOlderSave;
 A->bGlobalPowerService=E.bGlobalPower;A->bWorkshopService=E.bWorkshop;
 for(const auto& P:E.Ports){FReactiveLiquidPort Port;Port.PortId=P.Id;Port.TargetStableId=P.Target;Port.LocalPositionCm=P.From;Port.TargetLocalPositionCm=P.To;Port.MaxKgPerSecond=.1;A->Reactive->LiquidPorts.Add(Port);}

 return A;
}
void AAetherFrontierMode::BuildWorld()
{for(const auto& E:FAetherWorldDefinitions::Get().Objects)if(E.Section=="Frontier")SpawnPlacement(E);}
void AAetherFrontierMode::BuildWorkshop()
{for(const auto& E:FAetherWorldDefinitions::Get().Objects)if(E.Section=="Workshop")SpawnPlacement(E);RebuildWorldLinks();}
void AAetherFrontierMode::RebuildWorldLinks()
{
 for(const auto& E:FAetherWorldDefinitions::Get().Objects)if(auto* A=Prop(E.Id))
 {
  A->Mechanism->Supports.Reset();for(auto Id:E.Supports)if(auto* Support=Prop(Id))A->Mechanism->Supports.Add(Support->Reactive);
  if(!E.Supports.IsEmpty())A->Mesh->SetMassOverrideInKg(NAME_None,60,true);
  if(!E.Hinge.IsNone()&&!A->Mechanism->Constraint)if(auto* Anchor=Prop(E.Hinge))
  {
   A->Mesh->SetSimulatePhysics(true);A->Mesh->SetMassOverrideInKg(NAME_None,35,true);
   auto* Joint=NewObject<UPhysicsConstraintComponent>(A);A->AddInstanceComponent(Joint);Joint->RegisterComponent();Joint->SetWorldLocation(Anchor->GetActorLocation());
   Joint->SetDisableCollision(true);Joint->SetLinearXLimit(LCM_Locked,0);Joint->SetLinearYLimit(LCM_Locked,0);Joint->SetLinearZLimit(LCM_Locked,0);
   Joint->SetAngularSwing1Limit(ACM_Limited,80);Joint->SetAngularSwing2Limit(ACM_Locked,0);Joint->SetAngularTwistLimit(ACM_Locked,0);
   Joint->SetAngularDriveMode(EAngularDriveMode::TwistAndSwing);Joint->SetAngularOrientationDrive(true,false);Joint->SetAngularDriveParams(5000,500,20000);
   Joint->SetConstrainedComponents(Anchor->Mesh,NAME_None,A->Mesh,NAME_None);A->Mechanism->Constraint=Joint;
  }
 }
}
