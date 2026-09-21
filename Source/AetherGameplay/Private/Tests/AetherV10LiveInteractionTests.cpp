#include "Misc/AutomationTest.h"
#include "Interaction/AetherNearbyRegistry.h"
#include "Framework/AetherFrontier.h"
#include "Quests/AetherGuide.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
AAetherFrontierProp* MakeTarget(UWorld* W,FName Id,FName Service,FVector Position)
{
    FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* P=W->SpawnActor<AAetherFrontierProp>(Position,FRotator::ZeroRotator,Params);
    P->Spec.Id=Id;P->Spec.bInteractiveMaterial=false;P->Service=Service;P->DispatchBeginPlay();
    // 只保留下面独立创建的遮挡体，避免占位立方体彼此挡住视线干扰目标评分断言。
    P->Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);return P;
}
AAetherFrontierCharacter* MakeViewer(UWorld* W)
{
    auto* C=W->SpawnActor<AAetherFrontierCharacter>();
    auto* PS=W->SpawnActor<AAetherPlayerState>();PS->Profile.CharacterId=TEXT("TargetViewer");
    C->SetPlayerState(PS);C->BindPersistentAbilities();
    C->AbilitySystem->AddAttributeSetSubobject(C->Attributes.Get());C->AbilitySystem->InitAbilityActorInfo(PS,C);
    C->SetVitals(100,100,100);
    W->GetSubsystem<UAetherNearbyRegistry>()->Register(C);return C;
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherNearbyRegistryTest,"Aether.V10.Interaction.LiveSpatialRegistrationAndUnload",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherNearbyRegistryTest::RunTest(const FString&)
{
    auto* W=UWorld::CreateWorld(EWorldType::Game,false);
    if(!TestNotNull(TEXT("Isolated world"),W))return false;
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(W);
    ON_SCOPE_EXIT { W->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(W);W->DestroyWorld(false); };
    auto* Index=W->GetSubsystem<UAetherNearbyRegistry>();
    if(!TestNotNull(TEXT("World owns local spatial registry"),Index))return false;
    auto* P=MakeTarget(W,"Spatial.Test","Shop",FVector(-510,0,0));
    TestTrue(TEXT("Production BeginPlay registers target"),Index->Contains(P));
    TestTrue(TEXT("Negative-coordinate bucket query"),Index->Nearby(FVector(-520,0,0),20).Contains(P));
    Index->Register(P);
    TestEqual(TEXT("Duplicate registration has one spatial entry"),Index->Nearby(P->GetActorLocation(),20).Num(),1);
    P->SetActorLocation(FVector(1510,0,0));
    TestTrue(TEXT("Root transform update moves bucket immediately"),Index->Nearby(FVector(1510,0,0),20).Contains(P));
    TestTrue(TEXT("Old bucket cannot retain moved target"),Index->Nearby(FVector(-510,0,0),20).IsEmpty());
    TestTrue(TEXT("Unbounded query is refused"),Index->Nearby(FVector::ZeroVector,1001).IsEmpty());
    Index->Unregister(P);P->SetActorLocation(FVector(2000,0,0));
    TestFalse(TEXT("Unregister detaches transform callback"),Index->Contains(P));
    TestTrue(TEXT("Unregistered actor cannot be rediscovered by scan"),Index->Nearby(P->GetActorLocation(),20).IsEmpty());
    Index->Register(P);
    W->SetBegunPlay(true);P->Destroy();
    TestFalse(TEXT("Production EndPlay unregisters destroyed target"),Index->Contains(P));
    TestTrue(TEXT("No ghost candidate after unload"),Index->Nearby(FVector(2000,0,0),20).IsEmpty());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherPinnedInteractionTest,"Aether.V10.Interaction.PinnedSelectionHysteresisAndLiveGuards",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherPinnedInteractionTest::RunTest(const FString&)
{
    auto* W=UWorld::CreateWorld(EWorldType::Game,false);
    if(!TestNotNull(TEXT("Isolated interaction world"),W))return false;
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(W);
    ON_SCOPE_EXIT { W->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(W);W->DestroyWorld(false); };
    auto* C=MakeViewer(W);auto* PS=C->ProfileState();
    auto* A=MakeTarget(W,"Target.A","Shop",FVector(200,5,0));
    auto* B=MakeTarget(W,"Target.B","Inn",FVector(200,-5,0));
    const auto Seen=AetherGuide::QueryTarget(C,A);
    TestTrue(TEXT("Specified target is executable"),AetherGuide::ValidateSelection(C,Seen));
    TestTrue(TEXT("Focus hysteresis keeps equally good previous target"),AetherGuide::SelectInteraction(C,B).Prop==B);
    B->SetActorLocation(FVector(100,0,0));A->SetActorLocation(FVector(-180,0,0));
    TestTrue(TEXT("Clearly better view direction changes focus"),AetherGuide::SelectInteraction(C,A).Prop==B);
    TestTrue(TEXT("Server validates original A even when B is now preferred"),AetherGuide::ValidateSelection(C,Seen));
    auto Forged=Seen;Forged.ActionId="Inn";
    TestFalse(TEXT("Neighbor action cannot be applied to original target"),AetherGuide::ValidateSelection(C,Forged));
    ++PS->Profile.Revision;
    TestFalse(TEXT("Progress change invalidates old displayed choice"),AetherGuide::ValidateSelection(C,Seen));--PS->Profile.Revision;
    A->Service="Register";
    TestFalse(TEXT("Changed target action invalidates cached choice"),AetherGuide::ValidateSelection(C,Seen));A->Service="Shop";
    A->Spec.Id="Changed.Id";
    TestFalse(TEXT("Changed durable identity invalidates cached choice"),AetherGuide::ValidateSelection(C,Seen));A->Spec.Id="Target.A";
    A->SetActorLocation(FVector(251,0,0));
    TestFalse(TEXT("Out of range does not authorize nearby replacement"),AetherGuide::ValidateSelection(C,Seen));
    A->SetActorLocation(FVector(200,5,0));
    C->bTravelPending=true;
    TestFalse(TEXT("Travel revokes pending interaction"),AetherGuide::ValidateSelection(C,Seen));C->bTravelPending=false;

    auto* Wall=W->SpawnActor<AActor>();auto* Box=NewObject<UBoxComponent>(Wall);
    Wall->SetRootComponent(Box);Wall->AddInstanceComponent(Box);Box->SetBoxExtent(FVector(10,100,100));
    Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);Box->SetCollisionResponseToAllChannels(ECR_Block);Box->RegisterComponent();
    Wall->SetActorLocation(FVector(100,0,0));
    TestFalse(TEXT("New occlusion revokes same target"),AetherGuide::ValidateSelection(C,Seen));Box->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    TestTrue(TEXT("Cleared occlusion restores eligibility"),AetherGuide::ValidateSelection(C,Seen));

    A->Service="TrainingExtinguished";A->SetOwner(B);
    TestTrue(TEXT("Other player's personal object has no offer"),AetherGuide::QueryTarget(C,A).Prompt.IsEmpty());
    A->SetOwner(C);
    const auto Physical=AetherGuide::QueryTarget(C,A);
    TestFalse(TEXT("Physical fire hint cannot execute E"),Physical.bExecutable);
    TestFalse(TEXT("Forged E for physical-only object is rejected"),AetherGuide::ValidateSelection(C,Physical));
    A->Service=NAME_None;
    TestTrue(TEXT("Background object has no prompt"),AetherGuide::QueryTarget(C,A).Prompt.IsEmpty());

    // 进入真实旧服务分发器，验证它不会在校验后重新选择分数更高的 B。
    A->Service="HingedGate";B->Service="HingedGate";
    auto* Mode=W->SpawnActor<AAetherFrontierMode>();const auto GateSelection=AetherGuide::QueryTarget(C,A);
    A->Mechanism->bGateOpen=false;B->Mechanism->bGateOpen=false;
    Mode->InteractTarget(C,GateSelection);
    TestTrue(TEXT("Real dispatcher toggles only originally selected gate"),A->Mechanism->bGateOpen&&!B->Mechanism->bGateOpen);
    ++PS->Profile.Revision;Mode->InteractTarget(C,GateSelection);
    TestTrue(TEXT("Stale request cannot toggle either gate"),A->Mechanism->bGateOpen&&!B->Mechanism->bGateOpen);--PS->Profile.Revision;
    A->bEnabled=false;
    TestTrue(TEXT("Disabled target offers no prompt"),AetherGuide::QueryTarget(C,A).Prompt.IsEmpty());A->bEnabled=true;
    A->Service="Shop";
    W->SetBegunPlay(true);A->Destroy();
    auto* Replacement=MakeTarget(W,"Target.A","Shop",FVector(200,5,0));
    TestTrue(TEXT("Replacement with same stable ID is independently registered"),W->GetSubsystem<UAetherNearbyRegistry>()->Contains(Replacement));
    TestFalse(TEXT("Same stable ID cannot revive old Actor selection"),AetherGuide::ValidateSelection(C,Seen));
    TestTrue(TEXT("New instance needs a new selection"),AetherGuide::ValidateSelection(C,AetherGuide::QueryTarget(C,Replacement)));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherLiveTradeTest,"Aether.V10.Interaction.LiveMerchantLeaseAndTransactionalRetry",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherLiveTradeTest::RunTest(const FString&)
{
    using E=EAetherInventoryResult;auto* W=UWorld::CreateWorld(EWorldType::Game,false);
    if(!TestNotNull(TEXT("Isolated trading world"),W))return false;
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(W);
    ON_SCOPE_EXIT { W->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(W);W->DestroyWorld(false); };
    auto* C=MakeViewer(W);auto* PS=C->ProfileState();PS->Profile.Gold=100;
    auto* A=MakeTarget(W,"Merchant.A","Shop",FVector(180,0,0));
    auto* B=MakeTarget(W,"Merchant.B","Shop",FVector(150,40,0));
    auto* Mode=W->SpawnActor<AAetherFrontierMode>();
    Mode->Database=NewObject<UAetherFrontierSave>(Mode);Mode->Storage=AetherLocalSnapshotStore();
    Mode->SavePrefix=TEXT("V10Trade_")+FGuid::NewGuid().ToString(EGuidFormats::Digits);
    TestFalse(TEXT("Proximity alone is not authorization"),C->AuthorizeTrade(FGuid::NewGuid(),"Shop"));
    TestTrue(TEXT("Explicit interaction opens merchant session"),C->OpenTrade(A));
    const auto Token=C->TradeSession.Token;
    TestTrue(TEXT("Token binds actual merchant and owner"),C->AuthorizeTrade(Token,"Shop")&&C->TradeSession.Target==A);
    TestFalse(TEXT("Wrong token cannot use active merchant"),C->AuthorizeTrade(FGuid::NewGuid(),"Shop"));
    TestFalse(TEXT("Other catalog cannot reuse session"),C->AuthorizeTrade(Token,"Armorer"));
    PS->Profile.CharacterId=TEXT("ChangedOwner");
    TestFalse(TEXT("Character identity change invalidates lease"),C->AuthorizeTrade(Token,"Shop"));PS->Profile.CharacterId=TEXT("TargetViewer");
    FAetherInventoryCommand Buy;Buy.CommandId=FGuid::NewGuid();Buy.Action="Buy";Buy.DefinitionId="Potion";
    Buy.ShopId="Shop";Buy.Quantity=2;Buy.ExpectedInventoryRevision=0;
    const auto Execute=[&](const FAetherInventoryCommand& Request,FGuid Authorization)
    {int32 Rev=0,Moved=0;C->NextServerAction=0;return Mode->ExecuteInventory(C,Request,Rev,Moved,Authorization);};
    TestTrue(TEXT("Unscoped inventory route cannot buy"),Execute(Buy,{})==E::OutOfReach);
    Mode->bFailWrites=true;
    TestTrue(TEXT("Storage failure rolls back price and quantity"),Execute(Buy,Token)==E::StorageUnavailable&&PS->Profile.Gold==100&&PS->Profile.Count("Potion")==0&&PS->Profile.Revision==0);
    Mode->bFailWrites=false;
    TestTrue(TEXT("Same command retries to committed purchase"),Execute(Buy,Token)==E::Applied&&PS->Profile.Gold==60&&PS->Profile.Count("Potion")==2&&PS->Profile.Revision==1);
    TestTrue(TEXT("Actual slot and checksum published"),Mode->Storage->IsCommitted(Mode->SavePrefix+TEXT("1"),1));
    C->CloseTrade();
    TestTrue(TEXT("Committed receipt remains replayable after session closes"),Execute(Buy,Token)==E::Applied&&PS->Profile.Gold==60&&PS->Profile.Count("Potion")==2);
    auto NewBuy=Buy;NewBuy.CommandId=FGuid::NewGuid();NewBuy.ExpectedInventoryRevision=1;
    TestTrue(TEXT("Closed token cannot create new transaction"),Execute(NewBuy,Token)==E::OutOfReach);
    C->OpenTrade(A);const auto NewToken=C->TradeSession.Token;
    C->ClientTradeClosed_Implementation(Token);
    TestTrue(TEXT("Delayed close cannot revoke newer session"),C->AuthorizeTrade(NewToken,"Shop"));
    A->SetActorLocation(FVector(300,0,0));C->MaintainTrade();
    TestFalse(TEXT("Nearby same-shop merchant cannot replace departed original"),C->AuthorizeTrade(NewToken,"Shop"));
    TestFalse(TEXT("Return does not reactivate revoked token"),C->TradeSession.Token.IsValid());
    A->SetActorLocation(FVector(180,0,0));C->OpenTrade(A);
    const auto BeforeCombat=C->TradeSession.Token;C->CastLockUntil=1;C->MaintainTrade();
    TestFalse(TEXT("Casting revokes active trade"),C->AuthorizeTrade(BeforeCombat,"Shop"));C->CastLockUntil=0;
    C->OpenTrade(A);const auto BeforeReplacement=C->TradeSession.Token;C->OpenTrade(B);
    TestFalse(TEXT("Opening another merchant revokes old token"),C->AuthorizeTrade(BeforeReplacement,"Shop"));
    TestTrue(TEXT("New session is explicitly bound to B"),C->TradeSession.Target==B);

    C->bPanel=true;C->Panel=1;C->SelectedInstance=PS->Profile.Inventory[0].InstanceId;C->InventoryQuantity=1;
    C->RequestSale();
    TestTrue(TEXT("First sale click previews without spending"),!C->SaleConfirmationText().IsEmpty()&&PS->Profile.Count("Potion")==2&&PS->Profile.Gold==60);
    C->InventoryQuantity=2;C->MaintainTrade();
    TestTrue(TEXT("Changing quantity cancels confirmation"),C->SaleConfirmationText().IsEmpty());
    C->RequestSale();++PS->Profile.Revision;C->MaintainTrade();--PS->Profile.Revision;
    TestTrue(TEXT("Changed profile invalidates even when old selection returns"),C->SaleConfirmationText().IsEmpty());
    C->InventoryQuantity=1;C->RequestSale();C->SaleConfirmation.ExpiresAt=-1;C->MaintainTrade();
    TestTrue(TEXT("Expired confirmation cannot submit"),C->SaleConfirmationText().IsEmpty());

    // UI 确认表达用户意图；真实交易仍在服务器按规则重算价格，并先保存后发布。
    auto Sale=NewBuy;Sale.CommandId=FGuid::NewGuid();Sale.Action="Sell";Sale.DefinitionId=NAME_None;
    Sale.ItemInstanceId=C->SelectedInstance;Sale.Quantity=1;
    TestTrue(TEXT("Authorized sale commits quantity and gold together"),Execute(Sale,C->TradeSession.Token)==E::Applied&&PS->Profile.Gold==65&&PS->Profile.Count("Potion")==1);
    TestTrue(TEXT("Sale retry does not pay twice"),Execute(Sale,C->TradeSession.Token)==E::Applied&&PS->Profile.Gold==65);
    W->SetBegunPlay(true);B->Destroy();C->MaintainTrade();
    TestFalse(TEXT("Unloaded merchant revokes lease"),C->TradeSession.Token.IsValid());
    TestTrue(TEXT("Persisted sale receipt survives merchant destruction"),Execute(Sale,BeforeReplacement)==E::Applied);
    return true;
}
#endif
