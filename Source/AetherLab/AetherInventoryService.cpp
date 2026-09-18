#include "AetherFrontier.h"
#include "EngineUtils.h"
#include "Components/StaticMeshComponent.h"

void AAetherFrontierCharacter::SubmitInventory(FName Action,FName Definition)
{
 auto* PS=ProfileState();if(!PS)return;
 if(PendingInventory.CommandId.IsValid()){ServerInventory(PendingInventory);return;}
 if(PS->Profile.Revision<MinimumInventoryRevision){Notify(TEXT("等待背包同步后再操作。"));return;}
 const auto& P=PS->Profile;
 FAetherInventoryCommand C;C.CommandId=FGuid::NewGuid();C.ExpectedInventoryRevision=P.Revision;C.Action=Action;C.DefinitionId=Definition;
 if(Action=="Buy"||Action=="Sell")
 {
  double Best=FMath::Square(260.);for(TActorIterator<AAetherFrontierProp> It(GetWorld());It;++It)
   if(FAetherRules::Get().Shops.Contains(It->Service)){double D=FVector::DistSquared(GetActorLocation(),It->GetActorLocation());if(D<Best){Best=D;C.ShopId=It->Service;}}
 }
 C.ItemInstanceId=SelectedInstance;C.Quantity=(Action=="Split"||Action=="Merge"||Action=="Sell")?InventoryQuantity:1;C.DestinationInstanceId=MergeDestination;
 if(!Definition.IsNone()&&Action=="Use")for(const auto& Item:P.Inventory)if(Item.DefinitionId==Definition){C.ItemInstanceId=Item.InstanceId;break;}
 if(Action=="CycleMain"||Action=="CycleOff")
 {
  const FName Slot=Action=="CycleMain"?FName("MainHand"):FName("OffHand");
  TArray<FGuid> Choices;for(const auto& Item:P.Inventory)if(const auto* R=FAetherRules::Get().Items.Find(Item.DefinitionId);R&&R->bPlayerEquippable&&R->Slot==Slot)Choices.Add(Item.InstanceId);
  if(Choices.IsEmpty())return;
  const FGuid Current=P.Equipped.FindRef(Slot);C.ItemInstanceId=Choices[(Choices.Find(Current)+1)%Choices.Num()];C.Action="Equip";
  if(Action=="CycleOff"&&Current.IsValid()){C.Action="Unequip";C.ItemInstanceId=Current;}
 }
 PendingInventory=C;ServerInventory(C);
}
void AAetherFrontierCharacter::InventoryResult_Implementation(FGuid Id,EAetherInventoryResult Result,int32 Revision,int32 Transferred)
{
 if(PendingInventory.CommandId!=Id)return;
 MinimumInventoryRevision=FMath::Max(MinimumInventoryRevision,Revision);
 // A failed write may be retried with the same command. Rejections clear selection ambiguity.
 if(Result!=EAetherInventoryResult::StorageUnavailable&&Result!=EAetherInventoryResult::NotReady)PendingInventory=FAetherInventoryCommand();
 switch(Result)
 {
 case EAetherInventoryResult::Applied:Feedback=FString::Printf(TEXT("操作完成，数量 %d。"),Transferred);break;
 case EAetherInventoryResult::StaleRevision:case EAetherInventoryResult::MissingInstance:Feedback=TEXT("背包已变化，请重新选择物品。");break;
 case EAetherInventoryResult::StorageUnavailable:Feedback=TEXT("暂时无法保存，再次操作可重试。");break;
 case EAetherInventoryResult::NotReady:Feedback=TEXT("当前无法操作，请稍后重试。");break;
 case EAetherInventoryResult::OutOfReach:Feedback=TEXT("请靠近商人并保持通路畅通。");break;
 case EAetherInventoryResult::Capacity:Feedback=TEXT("目标没有足够空间，请整理背包。");break;
 case EAetherInventoryResult::InsufficientFunds:Feedback=TEXT("金币不足。");break;
 default:Feedback=TEXT("无法执行，请检查选中物品、数量和目标。");break;
 }
}
void AAetherFrontierCharacter::ServerInventory_Implementation(FAetherInventoryCommand C)
{
 auto* Mode=GetWorld()->GetAuthGameMode<AAetherFrontierMode>();auto* PS=ProfileState();if(!Mode||!PS)return;
 int32 Revision=PS->Profile.Revision,Moved=0;
 const auto Result=Mode->ExecuteInventory(this,C,Revision,Moved);
 InventoryResult(C.CommandId,Result,Revision,Moved);
}
EAetherInventoryResult AAetherFrontierMode::ExecuteInventory(AAetherFrontierCharacter* C,const FAetherInventoryCommand& Command,int32& Revision,int32& Moved)
{
 using E=EAetherInventoryResult;Moved=0;auto* PS=C?C->ProfileState():nullptr;if(!PS)return E::NotAllowed;Revision=PS->Profile.Revision;
 if(const auto* Receipt=PS->Profile.InventoryReceipts.FindByPredicate([&](const auto& R){return R.Command.CommandId==Command.CommandId;}))
 {if(!Receipt->Command.SameRequest(Command))return E::CommandConflict;Revision=Receipt->FinalRevision;Moved=Receipt->Transferred;return E::Applied;}
 if(C->bTravelPending||!C->Alive()||C->CombatTime()<C->NextServerAction)return E::NotReady;
 C->NextServerAction=C->CombatTime()+.12f;
 if(Command.Action=="Equip"||Command.Action=="Unequip")if(!C->Ready()||C->Carried)return E::NotReady;
 const auto& Rules=FAetherRules::Get();const FAetherUseRule* Use=nullptr;
 if(Command.Action=="Use")
 {
  Use=AetherItems::Use(PS->Profile,Command.ItemInstanceId,Rules);if(!Use)return E::NotAllowed;
  if(C->CombatTime()<C->NextPotion||C->TimeSinceDamage()<Use->SafeSeconds)return E::NotReady;
  if((Use->Health<=0||C->Health()>=C->MaxHealth)&&(Use->Mana<=0||C->Mana()>=100)&&(Use->Stamina<=0||C->Stamina()>=100))return E::NotAllowed;
 }
 if(Command.Action=="Buy"||Command.Action=="Sell")
 {
  bool Reach=false;
  for(const auto& P:Props)if(IsValid(P)&&P->Service==Command.ShopId&&Rules.Shops.Contains(P->Service)&&FVector::DistSquared(C->GetActorLocation(),P->GetActorLocation())<=FMath::Square(260.))
  {FCollisionQueryParams Q(SCENE_QUERY_STAT(InventoryShop),false,C);Q.AddIgnoredActor(P);if(!GetWorld()->LineTraceTestByChannel(C->GetActorLocation(),P->GetActorLocation(),ECC_Visibility,Q)){Reach=true;break;}}
  if(!Reach)return E::OutOfReach;
 }
 auto Next=PS->Profile;auto Result=AetherItems::Prepare(Next,Command,Rules,Moved);if(Result!=E::Applied)return Result;
 if(Next.Revision==MAX_int32)return E::NotAllowed;
 FAetherInventoryReceipt Receipt;Receipt.Command=Command;Receipt.FinalRevision=Next.Revision+1;Receipt.Transferred=Moved;
 // Older request versions remain stale even after their detailed receipt expires.
 if(Next.InventoryReceipts.Num()>=64)Next.InventoryReceipts.RemoveAt(0);
 Next.InventoryReceipts.Add(Receipt);
 if(!Commit(PS,Next)){Moved=0;return E::StorageUnavailable;}
 Revision=PS->Profile.Revision;
 if(Use){C->SetVitals(C->Health()+Use->Health,C->Mana()+Use->Mana,C->Stamina()+Use->Stamina);C->NextPotion=C->CombatTime()+Use->Cooldown;}
 if(Command.Action=="Equip"||Command.Action=="Unequip")C->ApplyProfileEquipment();
 return E::Applied;
}
