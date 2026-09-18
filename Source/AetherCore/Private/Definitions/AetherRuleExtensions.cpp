#include "Definitions/AetherRules.h"
#include "Dom/JsonObject.h"
bool AetherReadExtensions(FAetherRules& R,const TSharedPtr<FJsonObject>& Root)
{
 auto Fail=[&](const FString& Where){R.Error=TEXT("Rules: ")+Where;return false;};
 auto Number=[](const TSharedPtr<FJsonObject>& O,const TCHAR* Key,double& Value,double Max){return O->TryGetNumberField(Key,Value)&&FMath::IsFinite(Value)&&Value>=0&&Value<=Max;};
 double Version=0,Capacity=0;
 if(!Number(Root,TEXT("SchemaVersion"),Version,2)||Version!=2||!Number(Root,TEXT("InventoryCapacity"),Capacity,256)||Capacity<1||Capacity!=FMath::FloorToDouble(Capacity))return Fail(TEXT("SchemaVersion/InventoryCapacity"));
 R.SchemaVersion=2;R.InventoryCapacity=int32(Capacity);
 const TSharedPtr<FJsonObject> *Uses=nullptr,*Shops=nullptr,*Loot=nullptr;
 if(!Root->TryGetObjectField(TEXT("Uses"),Uses)||!Root->TryGetObjectField(TEXT("Shops"),Shops)||!Root->TryGetObjectField(TEXT("LootTables"),Loot))return Fail(TEXT("Uses/Shops/LootTables missing"));
 for(const auto& P:(*Uses)->Values)
 {
  auto O=P.Value->AsObject();FAetherUseRule U;
  if(!O||P.Key.IsEmpty()||!Number(O,TEXT("Health"),U.Health,1000)||!Number(O,TEXT("Mana"),U.Mana,1000)||!Number(O,TEXT("Stamina"),U.Stamina,1000)||!Number(O,TEXT("Cooldown"),U.Cooldown,3600)||!Number(O,TEXT("SafeSeconds"),U.SafeSeconds,3600)||U.Health+U.Mana+U.Stamina<=0)return Fail(TEXT("Uses.")+FString(*P.Key));
  R.Uses.Add(*P.Key,U);
 }
 const auto& Items=Root->GetObjectField(TEXT("Items"));
 for(const auto& P:Items->Values)
 {
  FString Id;if(P.Value->AsObject()->TryGetStringField(TEXT("Use"),Id)){if(!R.Uses.Contains(*Id))return Fail(TEXT("Items.")+FString(*P.Key)+TEXT(".Use"));R.Items.FindChecked(*P.Key).UseId=*Id;}
 }
 for(const auto& P:(*Shops)->Values)
 {
  const TArray<TSharedPtr<FJsonValue>>* List=nullptr;if(P.Key.IsEmpty()||!P.Value->TryGetArray(List)||List->IsEmpty()||List->Num()>128)return Fail(TEXT("Shops.")+FString(*P.Key));
  TArray<FName> Names;for(const auto& V:*List){FString Name;if(!V->TryGetString(Name)||!R.Items.Contains(*Name)||R.Items.FindChecked(*Name).Buy<=0||Names.Contains(*Name))return Fail(TEXT("Shops.")+FString(*P.Key));Names.Add(*Name);}R.Shops.Add(*P.Key,Names);
 }
 for(const auto& P:(*Loot)->Values)
 {
  auto O=P.Value->AsObject();if(!O||O->Values.IsEmpty()||P.Key.IsEmpty())return Fail(TEXT("LootTables.")+FString(*P.Key));
  TMap<FName,int32> Values;for(const auto& I:O->Values){double Q=0;if(!I.Value->TryGetNumber(Q)||!FMath::IsFinite(Q)||Q<1||Q>1000||Q!=FMath::FloorToDouble(Q)||!R.Items.Contains(*I.Key))return Fail(TEXT("LootTables.")+FString(*P.Key)+TEXT(".")+FString(*I.Key));Values.Add(*I.Key,int32(Q));}R.LootTables.Add(*P.Key,Values);
 }
 for(auto& P:R.Encounters){const auto& O=Root->GetObjectField(TEXT("Encounters"))->GetObjectField(P.Key.ToString());FString Id;if(!O->TryGetStringField(TEXT("LootTable"),Id)||!R.LootTables.Contains(*Id))return Fail(TEXT("Encounters.")+P.Key.ToString()+TEXT(".LootTable"));P.Value.LootTable=*Id;}
 const TArray<TSharedPtr<FJsonValue>>* Daily=nullptr;
 if(!Root->TryGetArrayField(TEXT("Dailies"),Daily)||Daily->Num()>16)return Fail(TEXT("Dailies"));
 auto ItemMap=[&](const TSharedPtr<FJsonObject>& O,const TCHAR* Field,TMap<FName,int32>& Out){const TSharedPtr<FJsonObject>* M=nullptr;if(!O->TryGetObjectField(Field,M))return false;for(const auto& P:(*M)->Values){double N=0;if(!P.Value->TryGetNumber(N)||!FMath::IsFinite(N)||N<1||N>1000||N!=FMath::FloorToDouble(N)||!R.Items.Contains(*P.Key))return false;Out.Add(*P.Key,int32(N));}return true;};
 TSet<FName> DailyIds,DailyServices;
 for(const auto& V:*Daily)
 {
  auto O=V->AsObject();FAetherDailyRule D;FString Id,Service,Gate;double Gold=0;const TArray<TSharedPtr<FJsonValue>>* Facts=nullptr;
  if(!O||!O->TryGetStringField(TEXT("Id"),Id)||Id.IsEmpty()||DailyIds.Contains(*Id)||!O->TryGetStringField(TEXT("Service"),Service)||Service.IsEmpty()||DailyServices.Contains(*Service)||!O->TryGetStringField(TEXT("QuestGate"),Gate)||!R.Quest(*Gate)||!O->TryGetArrayField(TEXT("Facts"),Facts)||!O->TryGetBoolField(TEXT("PersonalFires"),D.bPersonalFires)||!Number(O,TEXT("Gold"),Gold,10000)||Gold!=FMath::FloorToDouble(Gold)||!ItemMap(O,TEXT("Consume"),D.Consume)||!ItemMap(O,TEXT("Reward"),D.Reward))return Fail(TEXT("Dailies definition"));
  D.Id=*Id;D.Service=*Service;D.QuestGate=*Gate;D.Gold=int32(Gold);DailyIds.Add(D.Id);DailyServices.Add(D.Service);
  for(const auto& Fact:*Facts){FString Name;if(!Fact->TryGetString(Name)||Name.IsEmpty()||D.Facts.Contains(*Name))return Fail(TEXT("Dailies.Facts"));D.Facts.Add(*Name);}R.Dailies.Add(D);
 }
 const TSharedPtr<FJsonObject> *Requirements=nullptr,*Spells=nullptr;
 if(!Root->TryGetObjectField(TEXT("InteractionRequirements"),Requirements)||!Root->TryGetObjectField(TEXT("SpellUnlocks"),Spells))return Fail(TEXT("InteractionRequirements/SpellUnlocks"));
 for(const auto& P:(*Requirements)->Values){const TArray<TSharedPtr<FJsonValue>>* List=nullptr;if(!P.Value->TryGetArray(List))return Fail(TEXT("InteractionRequirements"));TArray<FName> IDs;for(const auto& V:*List){FString Id;if(!V->TryGetString(Id)||Id.IsEmpty())return Fail(TEXT("InteractionRequirements target"));IDs.AddUnique(*Id);}R.InteractionRequirements.Add(*P.Key,IDs);}
 for(const auto& P:(*Spells)->Values){double Mask=0;if(!P.Value->TryGetNumber(Mask)||!FMath::IsFinite(Mask)||Mask<1||Mask>15||Mask!=FMath::FloorToDouble(Mask)||!R.Quest(*P.Key))return Fail(TEXT("SpellUnlocks"));R.SpellUnlocks.Add(*P.Key,uint8(Mask));}
 return R.LootTables.Contains("Supply")&&R.LootTables.Contains("Gather");
}
