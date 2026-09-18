#include "AetherWorldDefinition.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
const FAetherWorldPlacement* FAetherWorldDefinitions::Find(FName Id) const
{return Objects.FindByPredicate([&](const auto& E){return E.Id==Id;});}
FAetherWorldDefinitions FAetherWorldDefinitions::Parse(const FString& Text)
{
 FAetherWorldDefinitions R;R.Error=TEXT("Invalid WorldObjects.json");
 TSharedPtr<FJsonObject> Root;const TSharedPtr<FJsonObject>* Defs=nullptr;const TArray<TSharedPtr<FJsonValue>>* Objects=nullptr;double Version=0;
 if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Root)||!Root||!Root->TryGetNumberField(TEXT("SchemaVersion"),Version)||Version!=1||!Root->TryGetObjectField(TEXT("Definitions"),Defs)||!Root->TryGetArrayField(TEXT("Objects"),Objects)||Objects->Num()>4096)return R;
 const TSet<FName> Known={"Metal","Carry","Impacts","PowerSource","PowerReceiver","Reactive","Moving","Bridge","Buoyant","LiquidSource"};
 auto Vector=[](const TSharedPtr<FJsonObject>& O,const TCHAR* Field,FVector& Out){const TArray<TSharedPtr<FJsonValue>>* V=nullptr;if(!O->TryGetArrayField(Field,V)||V->Num()!=3)return false;for(int I=0;I<3;++I){double N;if(!(*V)[I]->TryGetNumber(N)||!FMath::IsFinite(N)||FMath::Abs(N)>1000000)return false;Out[I]=N;}return true;};
 for(const auto& Pair:(*Defs)->Values)
 {
  auto O=Pair.Value->AsObject();const TArray<TSharedPtr<FJsonValue>>* Caps=nullptr;if(!O||!O->TryGetArrayField(TEXT("Capabilities"),Caps))return R;
  FAetherObjectDefinition D;for(const auto& V:*Caps){FString C;if(!V->TryGetString(C)||!Known.Contains(*C)||D.Capabilities.Contains(*C))return R;D.Capabilities.Add(*C);}
  O->TryGetNumberField(TEXT("PowerW"),D.PowerW);O->TryGetNumberField(TEXT("EnergyJ"),D.EnergyJ);O->TryGetNumberField(TEXT("ReceiverLoad"),D.ReceiverLoad);
  if(!FMath::IsFinite(D.PowerW)||!FMath::IsFinite(D.EnergyJ)||!FMath::IsFinite(D.ReceiverLoad)||D.PowerW<0||D.PowerW>100000||D.EnergyJ<0||D.EnergyJ>1.e10||D.ReceiverLoad<0||D.ReceiverLoad>100000)return R;
  R.Definitions.Add(*Pair.Key,D);
 }
 TSet<FName> IDs;
 for(const auto& V:*Objects)
 {
  auto O=V->AsObject();FAetherWorldPlacement E;FString Id,Definition,Service,Section,Hinge;double Kind=0;
  if(!O||!O->TryGetStringField(TEXT("Id"),Id)||Id.IsEmpty()||Id.Len()>128||IDs.Contains(*Id)||!O->TryGetStringField(TEXT("Definition"),Definition)||!R.Definitions.Contains(*Definition)||!O->TryGetStringField(TEXT("Service"),Service)||!O->TryGetStringField(TEXT("Section"),Section)||!O->TryGetStringField(TEXT("Label"),E.Label)||!O->TryGetNumberField(TEXT("Kind"),Kind)||Kind<0||Kind>6||Kind!=FMath::FloorToDouble(Kind)||!Vector(O,TEXT("Location"),E.Location)||!Vector(O,TEXT("Scale"),E.Scale)||E.Scale.GetMin()<=0||E.Scale.GetMax()>1000||!O->TryGetBoolField(TEXT("Stream"),E.bStream))return R;
  E.Id=*Id;E.Definition=*Definition;E.Service=*Service;E.Section=*Section;E.Kind=uint8(Kind);IDs.Add(E.Id);
  O->TryGetBoolField(TEXT("AllowAbsentFromOlderSave"),E.bAllowAbsentFromOlderSave);O->TryGetBoolField(TEXT("GlobalPower"),E.bGlobalPower);O->TryGetBoolField(TEXT("WorkshopService"),E.bWorkshop);O->TryGetNumberField(TEXT("Heat"),E.Heat);O->TryGetStringField(TEXT("Hinge"),Hinge);E.Hinge=*Hinge;
  if(!FMath::IsFinite(E.Heat)||E.Heat<0||E.Heat>1000000)return R;
  const TArray<TSharedPtr<FJsonValue>> *Supports=nullptr,*Ports=nullptr;
  if(!O->TryGetArrayField(TEXT("Supports"),Supports)||!O->TryGetArrayField(TEXT("Ports"),Ports))return R;
  for(const auto& S:*Supports){FString Name;if(!S->TryGetString(Name)||Name.IsEmpty()||E.Supports.Contains(*Name))return R;E.Supports.Add(*Name);}
  for(const auto& P:*Ports){auto PO=P->AsObject();FAetherWorldPort Port;FString Name,Target;if(!PO||!PO->TryGetStringField(TEXT("Id"),Name)||!PO->TryGetStringField(TEXT("Target"),Target)||!Vector(PO,TEXT("From"),Port.From)||!Vector(PO,TEXT("To"),Port.To))return R;Port.Id=*Name;Port.Target=*Target;E.Ports.Add(Port);}
  R.Objects.Add(E);
 }
 for(const auto& E:R.Objects){for(auto Id:E.Supports)if(Id==E.Id||!IDs.Contains(Id))return R;for(const auto& P:E.Ports)if(P.Target==E.Id||!IDs.Contains(P.Target))return R;if(!E.Hinge.IsNone()&&(!IDs.Contains(E.Hinge)||E.bStream))return R;}
 R.bValid=true;R.Error.Empty();return R;
}
const FAetherWorldDefinitions& FAetherWorldDefinitions::Get()
{
 static auto R=[](){FString Text;FFileHelper::LoadFileToString(Text,*(FPaths::ProjectContentDir()/TEXT("AetherCore/Definitions/WorldObjects.json")));return Parse(Text);}();return R;
}
