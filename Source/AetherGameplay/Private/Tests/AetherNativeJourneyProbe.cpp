#include "Tests/AetherNativeJourneyProbe.h"
#if !UE_BUILD_SHIPPING
#include "Framework/AetherPlayerController.h"
#include "Framework/AetherFrontier.h"
#include "Networking/AetherCommandClient.h"
#include "Interaction/AetherNativeInteraction.h"
#include "Contracts/AetherTransaction.h"
#include "World/AetherNativeContainer.h"
#include "Inventory/AetherResourceGate.h"
#include "Interaction/AetherWorldActionComponent.h"
#include "World/AetherFrontierState.h"
#include "Definitions/AetherV10Definitions.h"
#include "Quests/AetherGuide.h"
#include "Presentation/AetherMenuSubsystem.h"
#include "Engine/LocalPlayer.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/PlatformTime.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#endif
void AetherNativeJourneyProbe::Tick(AAetherPlayerController* PC)
{
#if !UE_BUILD_SHIPPING
 if(!FParse::Param(FCommandLine::Get(),TEXT("AetherNativeJourney"))||!PC||!PC->HasAuthority()||!PC->IsLocalController()||!PC->GetLocalPlayer())return;
 struct FState{int32 Step=0,Recoveries=0,GoldBefore=0,PointsBefore=0;double DeathAt=0;double Start=FPlatformTime::Seconds(),At=Start,Next=0,LogAt=0;bool Bound=false,Sent=false,Traveled=false,Done=false;FGuid Id,Original,Split,ServiceItem;FString Drop;TOptional<FAetherCommandResult> Reply;TArray<TSharedPtr<FJsonValue>> Steps;};
 static FState S;if(S.Done)return;const double Now=FPlatformTime::Seconds();
 const auto Fail=[&](const FString& Why){S.Done=true;UE_LOG(LogTemp,Error,TEXT("V10_JOURNEY_FAIL step=%d %s"),S.Step,*Why);FPlatformMisc::RequestExitWithStatus(false,1);};
 auto* Client=PC->GetLocalPlayer()->GetSubsystem<UAetherCommandClient>();
 if(!S.Bound)
 {
  FString Prefix;FParse::Value(FCommandLine::Get(),TEXT("AetherSavePrefix="),Prefix);
  if(!Prefix.StartsWith(TEXT("V10Journey_"))){Fail(TEXT("Isolated save required"));return;}
  S.Bound=true;Client->OnResult.AddWeakLambda(PC,[](const FAetherCommandResult& R){if(R.CommandId==S.Id)S.Reply=R;});
  IConsoleManager::Get().FindConsoleVariable(TEXT("aether.Motion.Backend"))->Set(0,ECVF_SetByCode);
 }
 if(Now-S.At>(S.Step==30?600:120)){Fail(TEXT("Step deadline"));return;}
 auto* C=Cast<AAetherFrontierCharacter>(PC->GetPawn());
 const bool LogNow=Now>S.LogAt;
 if(LogNow){S.LogAt=Now+5;UE_LOG(LogTemp,Display,TEXT("V10_JOURNEY_WAIT step=%d ready=%d travel=%d pending=%d channel=%d profile=%d container=%d drop=%s pos=%s"),S.Step,C&&C->Ready(),C&&C->bTravelPending,Client->HasPending(),Client->GetChannel().IsValid(),Client->GetProfile().IsSet(),Client->GetContainer().IsSet(),*S.Drop,C?*C->GetActorLocation().ToString():TEXT("none"));}
 if(!C||C->bTravelPending||!Client->GetProfile().IsSet()||!Client->GetChannel().IsValid()||Now<S.Next)return;
 if(LogNow&&C&&!C->Ready())UE_LOG(LogTemp,Display,TEXT("V10_JOURNEY_BLOCK health=%.2f gate=%d block=%d equipment=%d worldAction=%d cast=%.2f stun=%.2f avatar=%d"),C->Health(),C->ResourceGate->IsBlocked(),C->bBlocking,C->Equipment->IsBusy(),C->WorldActions->IsBusy(),C->CastLockUntil-C->CombatTime(),C->StunUntil-C->CombatTime(),C->AbilitySystem->GetAvatarActor()==C);
 // 正式死亡恢复创建新 Pawn/资源生命；夹具绝不直接补血或改写任务事实。
 if(!C->Alive()){
  if(S.Step==28||S.Step==29||S.Step==30){if(S.DeathAt==0)S.DeathAt=Now;if(Now-S.DeathAt>35)Fail(TEXT("Companions could not revive the player"));return;}
  if(C->TimeSinceDamage()<3)return;
  if(++S.Recoveries>3){Fail(TEXT("Repeated real player deaths"));return;}
  UE_LOG(LogTemp,Display,TEXT("V10_JOURNEY_RECOVER step=%d causer=%s"),S.Step,*GetNameSafe(C->CombatRuntime->LastDamager.Get()));
  S.Traveled=false;S.Sent=false;S.Reply.Reset();S.Next=Now+2;C->ServerAction(TEXT("Recover"),0);return;
 }
 S.DeathAt=0;
 const auto& P=Client->GetProfile().GetValue();
 const auto Advance=[&](const TCHAR* Name)
 {
  auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("step"),Name);Row->SetNumberField(TEXT("revision"),double(P.Revision));
  Row->SetNumberField(TEXT("gold"),P.Gold);S.Steps.Add(MakeShared<FJsonValueObject>(Row));
  UE_LOG(LogTemp,Display,TEXT("V10_JOURNEY_STEP %d %s revision=%lld"),S.Step,Name,P.Revision);
  S.Step=S.Step==27?32:S.Step==48?28:S.Step==31?49:S.Step+1;S.At=Now;S.Next=Now+.3;S.Sent=false;S.Traveled=false;S.Reply.Reset();C->ServerBlock(false);PC->GetLocalPlayer()->GetSubsystem<UAetherMenuSubsystem>()->Close();
 };
 const auto Send=[&](FAetherPlayerCommand Cmd)
 {
  if(!C->Ready()||Client->HasPending())return false;
  Cmd.ProtocolVersion=AetherCommands::LatestProtocolVersion;Cmd.ExpectedProfileRevision=P.Revision;Cmd.CommandId=AetherTransactions::NewCommandId(P.Revision);
  S.Id=Cmd.CommandId;S.Reply.Reset();FString Why;TArray<uint8> Bytes;
  if(!AetherCommands::Encode(Cmd,Bytes,Why)||!Client->Submit(Client->GetChannel(),Client->GetOwnerIdentity(),Bytes,Why)){Fail(Why);return false;}
  S.Sent=true;return true;
 };
 const auto Applied=[&]()
 {
  if(!S.Reply.IsSet()||Client->HasPending())return false;
  if(S.Reply->Code!=EAetherCommandCode::Applied&&S.Reply->Code!=EAetherCommandCode::Replayed){Fail(FString::Printf(TEXT("Command rejected code=%d"),int32(S.Reply->Code)));return false;}
  return true;
 };
 const auto Find=[&](const TCHAR* Service)->AAetherFrontierProp*
 {for(TActorIterator<AAetherFrontierProp> It(PC->GetWorld());It;++It)if(It->Service==Service&&It->bEnabled)return *It;return nullptr;};
 const auto Near=[&](AActor* Target,FVector Anchor)
 {
  if(!S.Traveled)
  {
   if(!C->Ready()||Client->HasPending())return false;
   C->GetCharacterMovement()->StopMovementImmediately();
   C->BeginSafeTravel((Target?Target->GetActorLocation():Anchor)+FVector(-100,0,130));S.Traveled=true;S.Next=Now+1;return false;
  }
  return Target&&C->Ready()&&!C->bTravelPending;
 };
 const auto Interaction=[&](AAetherFrontierProp* Target,const TCHAR* Action)
 {
  if(!Target||S.Sent||Client->HasPending()||!C->Ready())return;
  auto Provider=AetherNativeInteraction::Provider(*C,*Target);if(!Provider.IsSet())return;
  const auto Offers=Provider->Query({P.CharacterId,Target->Spec.Id.ToString()});
  const auto* Offer=Offers.FindByPredicate([&](const auto& O){return O.ActionId==Action&&O.Availability==EAetherOfferAvailability::Available;});
  if(!Offer)return;
  FString Why;FAetherInteractionSelection Selection{Offer->TargetStableId,Offer->ActionId,Offer->ProfileRevision,Offer->WorldRevision,Offer->TargetRevision};
  if(!AetherNativeInteraction::Submit(*C,Selection,Why)){Fail(Why);return;}S.Sent=true;S.Next=Now+.5;
 };
 if(S.Step==0)
 {
  if(!S.Sent){const auto* Item=P.Inventory.Items.FindByPredicate([](const auto& I){return I.DefinitionId==TEXT("Potion");});if(!Item||Item->Quantity!=2){Fail(TEXT("Expected normal new-player potion stack"));return;}S.Original=Item->InstanceId;FAetherPlayerCommand Cmd;Cmd.Type=EAetherCommandType::SplitStack;Cmd.ItemInstanceId=S.Original;Cmd.Quantity=1;Cmd.DestinationIndex=31;Send(Cmd);return;}
  if(Applied()){const auto* I=P.Inventory.At(31);const auto* O=P.Inventory.Find(S.Original);if(!I||!O||I->Quantity!=1||O->Quantity!=1||I->InstanceId==O->InstanceId){Fail(TEXT("Split identity or quantity"));return;}S.Split=I->InstanceId;Advance(TEXT("SplitStack"));}return;
 }
 if(S.Step==1)
 {
  if(!S.Sent){FAetherPlayerCommand Cmd;Cmd.Type=EAetherCommandType::MergeStack;Cmd.ItemInstanceId=S.Split;Cmd.OtherInstanceId=S.Original;Cmd.Quantity=1;Send(Cmd);return;}
  if(Applied()){const auto* I=P.Inventory.Find(S.Original);if(!I||I->Quantity!=2||P.Inventory.Find(S.Split)){Fail(TEXT("Merge conservation"));return;}Advance(TEXT("MergeStack"));}return;
 }
 if(S.Step==2)
 {
  if(!S.Sent){FAetherPlayerCommand Cmd;Cmd.Type=EAetherCommandType::DropItem;Cmd.ExpectedWorldRevision=PC->GetWorld()->GetGameState<AAetherFrontierState>()->NativeWorldRevision;Cmd.ItemInstanceId=S.Original;Cmd.Quantity=1;if(Send(Cmd))S.Drop=TEXT("Drop_")+S.Id.ToString(EGuidFormats::Digits);return;}
  if(Applied()){const auto* I=P.Inventory.Find(S.Original);if(!I||I->Quantity!=1){Fail(TEXT("Drop quantity"));return;}Advance(TEXT("DropItem"));}return;
 }
 if(S.Step==3)
 {
  if(!S.Sent)
  {
   if(!S.Traveled){AAetherNativeContainer* Box=nullptr;for(TActorIterator<AAetherNativeContainer> It(PC->GetWorld());It;++It)if(It->StableId==S.Drop){Box=*It;break;}if(!Box)return;Near(Box,Box->GetActorLocation());return;}
   if(!Client->GetContainer().IsSet()||Client->GetContainer()->ContainerId!=S.Drop){for(TActorIterator<AAetherNativeContainer> It(PC->GetWorld());It;++It)if(It->StableId==S.Drop){
 FHitResult Hit;FCollisionQueryParams Params(SCENE_QUERY_STAT(JourneyReach),false,C);Params.AddIgnoredActor(*It);
 const bool Blocked=PC->GetWorld()->LineTraceSingleByChannel(Hit,C->GetActorLocation(),It->GetActorLocation(),ECC_Visibility,Params);
 UE_LOG(LogTemp,Display,TEXT("V10_JOURNEY_CONTAINER distance=%.2f pos=%s blocked=%d hit=%s"),FVector::Distance(C->GetActorLocation(),It->GetActorLocation()),*It->GetActorLocation().ToString(),Blocked,*GetNameSafe(Hit.GetActor()));}
 const bool Opened=Client->OpenContainer(S.Drop);if(!Opened){Fail(TEXT("Cannot request container view"));return;}S.Next=Now+2.5;return;}
   const auto& Box=Client->GetContainer().GetValue();if(Box.Inventory.Items.Num()!=1){Fail(TEXT("Dropped item view"));return;}
   FAetherPlayerCommand Cmd;Cmd.Type=EAetherCommandType::PickUpItem;Cmd.ItemInstanceId=Box.Inventory.Items[0].InstanceId;Cmd.Quantity=1;
   Cmd.TargetStableId=S.Drop;Cmd.ExpectedWorldRevision=Client->GetContainerWorldRevision();Cmd.ExpectedContainerRevision=Box.Revision;Send(Cmd);return;
  }
  if(Applied()){int32 Count=0;for(const auto& I:P.Inventory.Items)if(I.DefinitionId==TEXT("Potion"))Count+=I.Quantity;if(Count!=2){Fail(TEXT("Pickup conservation"));return;}Client->CloseContainer();Advance(TEXT("PickUpItem"));}return;
 }
 struct FRoute{const TCHAR* Service;const TCHAR* Action;const TCHAR* Evidence;FVector Anchor;};
 static const FRoute Routes[]={
  {TEXT("SupplyA"),TEXT("Collect"),TEXT("SupplyA"),FVector(-6350,-28700,40)},
  {TEXT("SupplyB"),TEXT("Collect"),TEXT("SupplyB"),FVector(-6650,-28300,40)},
  {TEXT("Gate"),TEXT("Observe"),TEXT("Gate"),FVector(-6500,-9800,160)},
  {TEXT("Register"),TEXT("Register"),TEXT("Register"),FVector(-600,-700,90)},
  {TEXT("Inn"),TEXT("BindInn"),TEXT("Inn"),FVector(-400,-300,90)}
 };
 if(S.Step>=4&&S.Step<=8)
 {
  const auto& R=Routes[S.Step-4];if(P.Evidence.Contains(R.Evidence)){Advance(R.Evidence);return;}
  auto* Target=Find(R.Service);if(Near(Target,R.Anchor))Interaction(Target,R.Action);return;
 }
 if(S.Step==9||S.Step==10)
 {
  if(!P.Claims.Contains(TEXT("Q_Main_02"))){Fail(TEXT("Arrival/registration rewards not committed"));return;}
  const TCHAR* ItemId=S.Step==9?TEXT("TrainingSword"):TEXT("TrainingShield");const TCHAR* Slot=S.Step==9?TEXT("MainHand"):TEXT("OffHand");
  if(!S.Sent){const auto* I=P.Inventory.Items.FindByPredicate([&](const auto& Item){return Item.DefinitionId==ItemId;});if(!I){Fail(TEXT("Quest equipment missing"));return;}FAetherPlayerCommand Cmd;Cmd.Type=EAetherCommandType::EquipItem;Cmd.ItemInstanceId=I->InstanceId;Cmd.SlotId=Slot;Send(Cmd);return;}
  if(Applied()){if(!C->Equipment->InSlot(Slot)||C->Equipment->InSlot(Slot)->ItemId!=ItemId){Fail(TEXT("Equipment publication"));return;}Advance(ItemId);}return;
 }
 if(S.Step==11)
 {
  if(P.Skills.StoryGrants.Contains(TEXT("Water.Draw"))&&C->SkillUnlocked(TEXT("Water.Draw"))){Advance(TEXT("StorySkillGrant"));return;}
  auto* Target=Find(TEXT("Teacher"));if(Near(Target,FVector(400,-300,90)))Interaction(Target,TEXT("LearnStorySkills"));return;
 }
 if(S.Step==12)
 {
  if(Find(TEXT("TrainingExtinguished"))){Advance(TEXT("StartTraining"));return;}
  auto* Target=Find(TEXT("Teacher"));if(Near(Target,FVector(400,-300,90)))Interaction(Target,TEXT("Train"));return;
 }
 if(S.Step==13)
 {
  if(P.Evidence.Contains(TEXT("Melee3"))){Advance(TEXT("ThreeRealMeleeHits"));return;}
  auto* Target=Find(TEXT("Dummy"));if(!Near(Target,FVector(850,-300,90)))return;
  PC->SetControlRotation((Target->GetActorLocation()-C->GetActorLocation()).Rotation());C->ServerAttack(false);S.Next=Now+1;return;
 }
 if(S.Step==14)
 {
  if(P.Evidence.Contains(TEXT("Block"))){Advance(TEXT("RealShieldBlock"));return;}
  AAetherFrontierCharacter* Trainer=nullptr;const FName Tag(*(TEXT("Trainer_")+P.CharacterId));
  for(TActorIterator<AAetherFrontierCharacter> It(PC->GetWorld());It;++It)if(It->Tags.Contains(Tag)&&It->Alive()){Trainer=*It;break;}
  if(!Trainer){Fail(TEXT("Training opponent missing"));return;}
  if(!S.Traveled){if(Near(Trainer,Trainer->GetActorLocation())){}return;}
  PC->SetControlRotation((Trainer->GetActorLocation()-C->GetActorLocation()).Rotation());C->ServerBlock(true);
  Trainer->SetActorRotation((C->GetActorLocation()-Trainer->GetActorLocation()).Rotation());Trainer->ServerAttack(false);S.Next=Now+1;return;
 }
 if(S.Step==15)
 {
  if(P.Claims.Contains(TEXT("Q_Main_03"))){Advance(TEXT("TrainingQuestCommitted"));return;}
  auto* Target=Find(TEXT("TrainingExtinguished"));if(!Near(Target,C->GetActorLocation()))return;
  PC->SetControlRotation((Target->GetActorLocation()-(C->GetActorLocation()+FVector(0,0,55))).Rotation());
  if(C->TrySkill(TEXT("Water.Draw")))S.Next=Now+1;return;
 }

 const auto Fight=[&](AAetherFrontierCharacter* Enemy)
 {
  if(!Enemy||!Enemy->Alive())return;
  if(Client->HasPending())return;
  // 按普通玩家的正式施法和消耗品入口战斗，既不直接伤害敌人，也不注入资源。
  if(C->Health()<50&&C->Ready()){
   const auto* Potion=P.Inventory.Items.FindByPredicate([](const auto& I){return I.DefinitionId==TEXT("Potion");});
   if(Potion){FAetherPlayerCommand Cmd;Cmd.Type=EAetherCommandType::UseItem;Cmd.ItemInstanceId=Potion->InstanceId;Send(Cmd);S.Sent=false;S.Next=Now+.4;return;}
  }
  const FVector Delta=Enemy->GetActorLocation()-C->GetActorLocation();
  PC->SetControlRotation((Delta-FVector(0,0,55)).Rotation());
  if(C->Ready()&&Delta.Size2D()<1400){
   if(C->SkillUnlocked(TEXT("Frost.Freeze"))&&Enemy->Reactive->State.IceFraction<.4&&C->Mana()>=20&&C->TrySkill(TEXT("Frost.Freeze")))return;
   if(C->SkillUnlocked(TEXT("Storm.Strike"))&&C->Mana()>=25&&C->TrySkill(TEXT("Storm.Strike")))return;
  }
  const bool Defend=Delta.Size2D()<300&&(Enemy->bWindingUp||Enemy->Equipment->IsBusy());
  C->ServerBlock(Defend);
  if(!Defend&&C->Ready()){
   if(Delta.Size2D()>130)C->AddMovementInput(C->SafeMoveDirection(Enemy->GetActorLocation()));
   else C->ServerAttack(C->Stamina()>40);
  }
 };
 if(S.Step==16)
 {
  AAetherFrontierCharacter* Trainer=nullptr;const FName Tag(*(TEXT("Trainer_")+P.CharacterId));
  for(TActorIterator<AAetherFrontierCharacter> It(PC->GetWorld());It;++It)if(It->Tags.Contains(Tag)&&It->Alive()){Trainer=*It;break;}
  if(!Trainer){Advance(TEXT("TrainingOpponentDefeated"));return;}
  Fight(Trainer);return;
 }
 if(S.Step==17||S.Step==18)
 {
  if(C->HasRecentCombat(8))return;
  if(!S.Sent){FAetherPlayerCommand Cmd;Cmd.Type=S.Step==17?EAetherCommandType::UpgradeSkill:EAetherCommandType::BindSkill;Cmd.SkillId=TEXT("Water.Draw");if(S.Step==18)Cmd.SlotId=TEXT("Hotbar.2");Send(Cmd);return;}
  if(Applied()){
   if(S.Step==17&&P.Skills.PermanentRank(TEXT("Water.Draw"))!=2){Fail(TEXT("Purchased rank not published"));return;}
   Advance(S.Step==17?TEXT("UpgradeWaterRank2"):TEXT("BindWaterHotbar"));
  }return;
 }
 if(S.Step==19)
 {
  if(S.Sent&&C->WaterReserveKg>=2.99f&&C->Mana()>=99){Advance(TEXT("InnRestAndExternalGrant"));return;}
  auto* Target=Find(TEXT("Inn"));if(Near(Target,FVector(-400,-300,90)))Interaction(Target,TEXT("Rest"));return;
 }
 if(S.Step>=20&&S.Step<=22)
 {
  const FString Service=FString::Printf(TEXT("ForestFire%d"),S.Step-20);
  if(P.Evidence.Contains(Service)){Advance(*Service);return;}
  auto* Target=Find(*Service);if(!Near(Target,FVector(-26500-(S.Step-20)*450,350,50)))return;
  if(AetherGuide::CanInspectFire(Target)){Interaction(Target,TEXT("Inspect"));return;}
  if(C->WaterReserveKg<.5f){Fail(TEXT("Forest extinguishing exhausted real water supply"));return;}
  PC->SetControlRotation((Target->GetActorLocation()-(C->GetActorLocation()+FVector(0,0,55))).Rotation());
  if(C->TrySkill(TEXT("Water.Draw")))S.Next=Now+1.5;return;
 }
 if(S.Step==23||S.Step==24)
 {
  const bool Rescue=S.Step==23;const TCHAR* Quest=Rescue?TEXT("Q_Main_04"):TEXT("Q_Main_05");
  if(P.Claims.Contains(Quest)){Advance(Quest);return;}
  auto* Target=Find(Rescue?TEXT("Rescue"):TEXT("SupplyRestored"));
  if(Near(Target,Rescue?FVector(-27700,-300,90):FVector(28100,0,80)))Interaction(Target,Rescue?TEXT("Observe"):TEXT("Pump"));return;
 }
 if(S.Step==25)
 {
  if(P.Skills.StoryGrants.Contains(TEXT("Storm.Strike"))&&C->SkillUnlocked(TEXT("Frost.Freeze"))){Advance(TEXT("AdvancedStorySkills"));return;}
  auto* Target=Find(TEXT("Teacher"));if(Near(Target,FVector(400,-300,90)))Interaction(Target,TEXT("LearnStorySkills"));return;
 }
 if(S.Step==26||S.Step==27)
 {
  const FName Wanted=S.Step==26?TEXT("Lishi"):TEXT("Muhe");
  bool Found=false;for(TActorIterator<AAetherFrontierCharacter> It(PC->GetWorld());It;++It)Found|=It->CompanionOwner==C&&It->CompanionId==Wanted;
  if(Found&&P.Claims.Contains(TEXT("Q_Main_06"))){Advance(S.Step==26?TEXT("RecruitGuard"):TEXT("RecruitHealer"));return;}
  auto* Target=Find(TEXT("Recruit"));if(Near(Target,FVector(400,300,90)))Interaction(Target,S.Step==26?TEXT("Guard"):TEXT("Healer"));return;
 }
 if(S.Step==28)
 {
  // 带同伴沿北路真实移动；不在夹具里瞬移同伴或伪造修道院参与席位。
  const FVector Destination(0,24250,90);const FVector Delta=Destination-C->GetActorLocation();
  if(Delta.Size2D()>160){PC->SetControlRotation(Delta.Rotation());C->AddMovementInput(C->SafeMoveDirection(Destination));return;}
  C->GetCharacterMovement()->StopMovementImmediately();
  int32 NearBuddies=0;for(TActorIterator<AAetherFrontierCharacter> It(PC->GetWorld());It;++It)if(It->CompanionOwner==C&&It->Alive()&&FVector::DistSquared(It->GetActorLocation(),C->GetActorLocation())<FMath::Square(1000.))++NearBuddies;
  if(NearBuddies>=2)Advance(TEXT("PartyWalkedToAbbey"));return;
 }
 if(S.Step==29)
 {
  auto* Mode=PC->GetWorld()->GetAuthGameMode<AAetherFrontierMode>();
  if(Mode&&Mode->Encounters&&Mode->Encounters->Abbey.Phase!=EAetherEncounterPhase::Idle){Advance(TEXT("StartAbbey"));return;}
  auto* Target=Find(TEXT("Abbey"));if(!Target)return;
  const FVector Delta=Target->GetActorLocation()-C->GetActorLocation();
  if(Delta.Size2D()>140){PC->SetControlRotation(Delta.Rotation());C->AddMovementInput(C->SafeMoveDirection(Target->GetActorLocation()));return;}
  C->GetCharacterMovement()->StopMovementImmediately();Interaction(Target,TEXT("Start"));return;
 }
 if(S.Step==30)
 {
  if(P.Claims.Contains(TEXT("Q_Main_07"))){Advance(TEXT("AbbeyQuestCommitted"));return;}
  auto* Mode=PC->GetWorld()->GetAuthGameMode<AAetherFrontierMode>();if(!Mode||!Mode->Encounters)return;
  auto* Director=Mode->Encounters.Get();if(Director->Abbey.Phase==EAetherEncounterPhase::Failed){Fail(TEXT("Real abbey encounter failed"));return;}
  AAetherFrontierCharacter* Enemy=nullptr;double Best=DBL_MAX;
  for(const auto& E:Director->AbbeyEnemies)if(IsValid(E)&&E->Alive()){const double D=FVector::DistSquared(C->GetActorLocation(),E->GetActorLocation());if(D<Best){Best=D;Enemy=E;}}
  if(Enemy){Fight(Enemy);return;}
  C->ServerBlock(false);
  if(Director->Abbey.Phase==EAetherEncounterPhase::Channel)
  {
   auto* Valve=Find(TEXT("AbbeyValve"));if(!Valve)return;
   const FVector Delta=Valve->GetActorLocation()-C->GetActorLocation();
   if(Delta.Size2D()>120){PC->SetControlRotation(Delta.Rotation());C->AddMovementInput(C->SafeMoveDirection(Valve->GetActorLocation()));return;}
   C->GetCharacterMovement()->StopMovementImmediately();
   if(!Director->IsChanneling(C)){S.Sent=false;Interaction(Valve,TEXT("Channel"));}
  }return;
 }
 if(S.Step==31)
 {
  if(P.Claims.Contains(TEXT("Q_Main_08"))){Advance(TEXT("SealDeliveredAndMainlineCommitted"));return;}
  auto* Target=Find(TEXT("SealDelivered"));if(Near(Target,FVector(0,700,90)))Interaction(Target,TEXT("Observe"));return;
 }

 // 城镇准备同时覆盖真实买卖、穿脱外部授予、药剂投递、退款和个人仓储。
 if(S.Step>=32&&S.Step<=42)
 {
  using E=EAetherCommandType;
  struct FMarket{E Type;const TCHAR* Item;const TCHAR* Slot;};
  static const FMarket Ops[]={
   {E::BuyItem,TEXT("IronSword"),TEXT("")},{E::EquipItem,TEXT("IronSword"),TEXT("MainHand")},
   {E::BuyItem,TEXT("IronCuirass"),TEXT("")},{E::EquipItem,TEXT("IronCuirass"),TEXT("Chest")},
   {E::BuyItem,TEXT("CopperRing"),TEXT("")},{E::EquipItem,TEXT("CopperRing"),TEXT("Ring1")},
   {E::UnequipItem,TEXT("CopperRing"),TEXT("")},{E::SellItem,TEXT("CopperRing"),TEXT("")},
   {E::BuyItem,TEXT("ManaPotion"),TEXT("")},{E::BuyItem,TEXT("Ration"),TEXT("")},{E::SellItem,TEXT("Ration"),TEXT("")}};
  const auto& Op=Ops[S.Step-32];const bool Trade=Op.Type==E::BuyItem||Op.Type==E::SellItem;
  auto* Target=Find(S.Step>=40?TEXT("Shop"):TEXT("Armorer"));if(!Near(Target,C->GetActorLocation()))return;
  if(Trade&&C->ActiveShop()!=Target->Service){Interaction(Target,TEXT("Trade"));S.Sent=false;return;}
  if(!S.Sent){
   FAetherPlayerCommand Cmd;Cmd.Type=Op.Type;Cmd.SlotId=Op.Slot;S.GoldBefore=P.Gold;
   if(Trade){Cmd.TargetStableId=Target->Spec.Id.ToString();Cmd.Quantity=1;}
   if(Op.Type==E::BuyItem)Cmd.DefinitionId=Op.Item;
   else {const auto* Item=P.Inventory.Items.FindByPredicate([&](const auto& I){return I.DefinitionId==Op.Item;});if(!Item){Fail(TEXT("Market item missing"));return;}Cmd.ItemInstanceId=Item->InstanceId;S.ServiceItem=Item->InstanceId;}
   Send(Cmd);return;
  }
  if(Applied()){
   const auto* Definition=FAetherV10Definitions::Get().Items.Items.Find(Op.Item);
   if(!Definition){Fail(TEXT("Market definition missing"));return;}
   const int32 Expected=S.GoldBefore+(Op.Type==E::BuyItem?-Definition->BuyPrice:Op.Type==E::SellItem?Definition->SellPrice:0);
   if(P.Gold!=Expected){Fail(TEXT("Market currency conservation"));return;}
   if(S.Step==37||S.Step==38){
    bool GearGrant=false;for(const auto& Grant:C->ProfileState()->GetNativeSkillGrants())if(Grant.SkillId==TEXT("Frost.Freeze")&&Grant.Source==EAetherSkillGrantSource::Equipment)GearGrant=true;
    if(GearGrant!=(S.Step==37)){Fail(TEXT("Equipment skill grant/revoke publication"));return;}
   }
   Advance(*FString::Printf(TEXT("Market_%d_%s"),int32(Op.Type),Op.Item));
  }return;
 }
 if(S.Step==43)
 {
  if(!S.Sent){
   if(C->Mana()>80){
    // 霜凝对真实训练木桩施放，不依赖死亡后已经耗尽的水量，也不对无目标空放计成功。
    auto* Dummy=Find(TEXT("Dummy"));if(!Near(Dummy,FVector(850,-300,90)))return;
    PC->SetControlRotation((Dummy->GetActorLocation()-(C->GetActorLocation()+FVector(0,0,55))).Rotation());
    if(C->TrySkill(TEXT("Frost.Freeze")))S.Next=Now+1;
    return;
   }
   if(!C->Ready())return;
   const auto* Item=P.Inventory.Items.FindByPredicate([](const auto& I){return I.DefinitionId==TEXT("ManaPotion");});
   if(!Item){Fail(TEXT("Purchased mana potion missing"));return;}
   S.ServiceItem=Item->InstanceId;FAetherPlayerCommand Cmd;Cmd.Type=EAetherCommandType::UseItem;Cmd.ItemInstanceId=Item->InstanceId;Send(Cmd);return;
  }
  if(Applied()){if(P.Inventory.Find(S.ServiceItem)||C->Mana()<85){Fail(TEXT("Consumable inventory/resource publication"));return;}Advance(TEXT("RealManaPotionDelivery"));}return;
 }
 if(S.Step==44||S.Step==45)
 {
  auto* Teacher=Find(TEXT("Teacher"));if(!Near(Teacher,C->GetActorLocation())||C->HasRecentCombat(8))return;
  if(!S.Sent){S.PointsBefore=P.Skills.AvailableSkillPoints;FAetherPlayerCommand Cmd;Cmd.Type=S.Step==44?EAetherCommandType::ResetSkills:EAetherCommandType::UpgradeSkill;Cmd.SkillId=TEXT("Water.Draw");Send(Cmd);return;}
  if(Applied()){
   const bool Reset=S.Step==44;
   if(P.Skills.PermanentRank(TEXT("Water.Draw"))!=(Reset?1:2)||P.Skills.AvailableSkillPoints!=S.PointsBefore+(Reset?1:-1)){Fail(TEXT("Skill refund/relearn conservation"));return;}
   Advance(Reset?TEXT("ResetPurchasedWaterRank"):TEXT("RelearnWaterRank"));
  }return;
 }
 if(S.Step==46||S.Step==47)
 {
  const FString Id=TEXT("Storage_")+P.CharacterId;AAetherNativeContainer* Box=nullptr;
  for(TActorIterator<AAetherNativeContainer> It(PC->GetWorld());It;++It)if(It->StableId==Id){Box=*It;break;}
  if(!Box){Fail(TEXT("Personal storage actor missing"));return;}
  if(!Near(Box,Box->GetActorLocation()))return;
  if(!Client->GetContainer().IsSet()||Client->GetContainer()->ContainerId!=Id){Client->OpenContainer(Id);S.Next=Now+.5;return;}
  if(!S.Sent){
   const auto& View=Client->GetContainer().GetValue();const auto& Inventory=S.Step==46?P.Inventory:View.Inventory;
   const auto* Item=Inventory.Items.FindByPredicate([](const auto& I){return I.DefinitionId==TEXT("TrainingHammer");});
   if(!Item){Fail(TEXT("Storage item missing"));return;}S.ServiceItem=Item->InstanceId;
   FAetherPlayerCommand Cmd;Cmd.Type=EAetherCommandType::TransferItem;Cmd.ItemInstanceId=Item->InstanceId;Cmd.Quantity=1;
   Cmd.TargetStableId=Id;Cmd.ContainerId=Id;Cmd.ExpectedWorldRevision=Client->GetContainerWorldRevision();Cmd.ExpectedContainerRevision=View.Revision;
   Cmd.TransferDirection=S.Step==46?EAetherTransferDirection::IntoContainer:EAetherTransferDirection::FromContainer;Send(Cmd);return;
  }
  if(Applied()){
   const bool Found=P.Inventory.Find(S.ServiceItem)!=nullptr;if(Found!=(S.Step==47)){Fail(TEXT("Storage ownership publication"));return;}
   Client->CloseContainer();Advance(S.Step==46?TEXT("PersonalStorageDeposit"):TEXT("PersonalStorageWithdraw"));
  }return;
 }
 if(S.Step==48)
 {
  auto* Inn=Find(TEXT("Inn"));if(!Near(Inn,C->GetActorLocation()))return;
  if(S.Sent&&C->Health()>=C->MaxHealth-.01&&C->Mana()>=99){Advance(TEXT("EquippedPartyRest"));return;}
  Interaction(Inn,TEXT("Rest"));return;
 }
 if(S.Step==49)
 {
  auto* Shop=Find(TEXT("Armorer"));if(!Near(Shop,C->GetActorLocation())||C->HasRecentCombat(8))return;
  if(C->ActiveShop()!=Shop->Service){Interaction(Shop,TEXT("Trade"));S.Sent=false;return;}
  if(!S.Sent){
   const auto* Item=P.Inventory.Items.FindByPredicate([](const auto& I){return I.DefinitionId==TEXT("IronSword")&&I.Durability<120;});
   if(!Item){Fail(TEXT("Real battle did not produce expected sword wear"));return;}
   S.ServiceItem=Item->InstanceId;S.GoldBefore=P.Gold-(120-Item->Durability);
   FAetherPlayerCommand Cmd;Cmd.Type=EAetherCommandType::RepairItem;Cmd.ItemInstanceId=Item->InstanceId;Cmd.TargetStableId=Shop->Spec.Id.ToString();Send(Cmd);return;
  }
  if(Applied()){const auto* Item=P.Inventory.Find(S.ServiceItem);if(!Item||Item->Durability!=120||P.Gold!=S.GoldBefore){Fail(TEXT("Repair conservation"));return;}Advance(TEXT("RepairActualBattleWear"));}return;
 }
 for(int32 Q=1;Q<=8;++Q)if(!P.Claims.Contains(FString::Printf(TEXT("Q_Main_%02d"),Q))){Fail(TEXT("Missing committed mainline claim"));return;}

 auto Report=MakeShared<FJsonObject>();Report->SetBoolField(TEXT("passed"),true);Report->SetNumberField(TEXT("questCount"),8);
 Report->SetBoolField(TEXT("fullMainline"),true);Report->SetNumberField(TEXT("recoveries"),S.Recoveries);
 Report->SetStringField(TEXT("scope"),TEXT("Real production transactions/combat with safe-travel approach automation; human input and UI acceptance are separate."));Report->SetArrayField(TEXT("steps"),S.Steps);
 FString Json,Path;FParse::Value(FCommandLine::Get(),TEXT("AetherJourneyReport="),Path);FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Json));
 if(Path.IsEmpty()||!FFileHelper::SaveStringToFile(Json,*Path)){Fail(TEXT("Report write failed"));return;}
 S.Done=true;UE_LOG(LogTemp,Display,TEXT("V10_JOURNEY_PASS quests=8"));FPlatformMisc::RequestExit(false);
#endif
}
