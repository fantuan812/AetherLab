#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Combat/AetherCombat.h"
#include "Inventory/AetherResourceGate.h"
#include "Engine/World.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
AAetherCharacter* SpawnResourceSubject(UWorld* World,FVector Location)
{
    FActorSpawnParameters Parameters;Parameters.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* C=World->SpawnActor<AAetherCharacter>(Location,FRotator::ZeroRotator,Parameters);
    if(C)
    {
        C->AbilitySystem->AddAttributeSetSubobject(C->Attributes.Get());
        C->AbilitySystem->InitAbilityActorInfo(C,C);C->SetVitals(50,40,30);
    }
    return C;
}
bool PrepareDelivery(AAetherCharacter* C,FAetherEffectDelivery& Delivery)
{
    auto* Gate=C->ResourceGate.Get();
    if(!Gate->BeginFullRespawn(TEXT("ProjectionFixture"))||!Gate->FinishRecovery())return false;
    C->SetVitals(50,40,30);
    FAetherConsumableEffectV10 Effect;Effect.DeliveryId=FGuid(0,1,7,8);Effect.ItemInstanceId=FGuid(1,2,3,4);
    Effect.DefinitionId=TEXT("Potion");Effect.ProfileRevision=1;
    if(!Gate->Reserve(Effect.DeliveryId,Effect.Before))return false;
    Effect.After=Effect.Before;++Effect.After.Revision;Effect.After.Health+=10;Effect.After.Mana+=10;Effect.After.Stamina+=10;
    Delivery.Id=Effect.DeliveryId;Delivery.ActorId=TEXT("ProjectionFixture");
    return AetherConsumableEffects::Encode(Effect,Delivery.Payload)&&
        Gate->GetReceiver()->Apply(Delivery,Delivery.ActorId)==EAetherEffectApplyCode::Applied;
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherResourceProjectionLifecycleTest,"Aether.V10.Resources.ReentrantProjectionTarget",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherResourceProjectionLifecycleTest::RunTest(const FString&)
{
    auto* World=UWorld::CreateWorld(EWorldType::Game,false);
    if(!TestNotNull(TEXT("Isolated resource world"),World))return false;
    ON_SCOPE_EXIT {World->DestroyWorld(false);};
    int32 Fixture=0;
    AddExpectedError(TEXT("AETHER_RESOURCE_SESSION_FAILED Resource projection target changed"),EAutomationExpectedErrorFlags::Contains,6);
    // Exercise real GAS notifications at every write boundary in both entry points.
    for(int32 DeliveryMode=0;DeliveryMode<2;++DeliveryMode)for(int32 Boundary=0;Boundary<3;++Boundary)
    {
        auto* C=SpawnResourceSubject(World,FVector(++Fixture*1000,0,88));
        auto* Replacement=SpawnResourceSubject(World,FVector(++Fixture*1000,0,88));
        if(!C||!Replacement)return false;
        auto* Gate=C->ResourceGate.Get();auto* OriginalSystem=C->AbilitySystem.Get();auto* OriginalAttributes=C->Attributes.Get();
        FAetherEffectDelivery Delivery;
        if(DeliveryMode&&!TestTrue(TEXT("Prepare real reserved consumable receiver"),PrepareDelivery(C,Delivery)))return false;
        Replacement->SetVitals(41,23,17);
        const FGameplayAttribute Attribute=Boundary==0?UAetherAttributes::GetHealthAttribute():
            Boundary==1?UAetherAttributes::GetManaAttribute():UAetherAttributes::GetStaminaAttribute();
        bool Reentered=false;
        const auto Hook=OriginalSystem->GetGameplayAttributeValueChangeDelegate(Attribute).AddLambda([&](const FOnAttributeChangeData& Change){
            if(Reentered||Change.NewValue<=Change.OldValue)return;Reentered=true;
            C->AbilitySystem=Replacement->AbilitySystem;C->Attributes=Replacement->Attributes;
            // Keep the same avatar: checking only GetAvatarActor()==C would miss this replacement.
            C->AbilitySystem->InitAbilityActorInfo(C,C);
        });
        const bool Published=DeliveryMode?Gate->Publish(*Gate->GetReceiver()):Gate->BeginFullRespawn(TEXT("ProjectionFixture"));
        OriginalSystem->GetGameplayAttributeValueChangeDelegate(Attribute).Remove(Hook);
        TestTrue(TEXT("Actual attribute notification replaced the ASC"),Reentered&&C->AbilitySystem!=OriginalSystem);
        TestFalse(TEXT("Old resource batch cannot report successful publication"),Published);
        TestTrue(TEXT("Replacement resources were not overwritten"),C->Health()==41&&C->Mana()==23&&C->Stamina()==17);
        if(DeliveryMode)
        {
            TestTrue(TEXT("Failure retains reservation and delivery proof for recovery, never acknowledges"),
                Gate->Reservation()==Delivery.Id&&Gate->GetReceiver()->PendingAcknowledgementIds().Contains(Delivery.Id));
            TestFalse(TEXT("Retry cannot rebind the old receiver to the replacement ASC"),Gate->Publish(*Gate->GetReceiver()));
        }
        else TestNull(TEXT("Interrupted initialization creates no receiver"),Gate->GetReceiver());
        C->AbilitySystem=OriginalSystem;C->Attributes=OriginalAttributes;
        Replacement->AbilitySystem->InitAbilityActorInfo(Replacement,Replacement);
    }
    for(int32 DeliveryMode=0;DeliveryMode<2;++DeliveryMode)
    {
        auto* C=SpawnResourceSubject(World,FVector(++Fixture*1000,0,88));if(!C)return false;
        auto* Gate=C->ResourceGate.Get();auto* System=C->AbilitySystem.Get();FAetherEffectDelivery Delivery;
        if(DeliveryMode&&!PrepareDelivery(C,Delivery))return false;
        bool Reentered=false;
        const auto Hook=System->GetGameplayAttributeValueChangeDelegate(UAetherAttributes::GetHealthAttribute()).AddLambda([&](const FOnAttributeChangeData& Change){
            if(Reentered||Change.NewValue<=Change.OldValue)return;Reentered=true;Gate->EndPlay(EEndPlayReason::RemovedFromWorld);
        });
        const bool Published=DeliveryMode?Gate->Publish(*Gate->GetReceiver()):Gate->BeginFullRespawn(TEXT("ProjectionFixture"));
        System->GetGameplayAttributeValueChangeDelegate(UAetherAttributes::GetHealthAttribute()).Remove(Hook);
        TestTrue(TEXT("Gate ended during actual Health publication"),Reentered&&Gate->IsFaulted());
        TestFalse(TEXT("Ended gate cannot complete or recreate its receiver"),Published);
        TestNull(TEXT("Ended receiver stays absent"),Gate->GetReceiver());
        TestTrue(TEXT("No Mana/Stamina writes after EndPlay"),C->Mana()==40&&C->Stamina()==30);
    }
    auto* C=SpawnResourceSubject(World,FVector(++Fixture*1000,0,88));if(!C)return false;
    auto* Gate=C->ResourceGate.Get();bool Reentered=false,NestedAccepted=false;
    const auto Hook=C->AbilitySystem->GetGameplayAttributeValueChangeDelegate(UAetherAttributes::GetHealthAttribute()).AddLambda([&](const FOnAttributeChangeData& Change){
        if(Reentered||Change.NewValue<=Change.OldValue)return;Reentered=true;NestedAccepted=Gate->BeginFullRespawn(TEXT("NestedFixture"));
    });
    const bool Began=Gate->BeginFullRespawn(TEXT("ProjectionFixture"));
    C->AbilitySystem->GetGameplayAttributeValueChangeDelegate(UAetherAttributes::GetHealthAttribute()).Remove(Hook);
    TestTrue(TEXT("Normal initialization completes despite rejected nested entry"),Began&&Reentered&&!NestedAccepted&&Gate->FinishRecovery());
    TestTrue(TEXT("Only outer identity owns the full-resource receiver"),Gate->GetReceiver()&&Gate->GetReceiver()->CharacterId()==TEXT("ProjectionFixture")&&
        C->Health()==100&&C->Mana()==100&&C->Stamina()==100);
    FAetherEffectDelivery Delivery;C->SetVitals(50,40,30);
    // A fresh subject verifies the ordinary publish/replay path independently of rejected batches.
    C=SpawnResourceSubject(World,FVector(++Fixture*1000,0,88));if(!C)return false;Gate=C->ResourceGate.Get();
    if(!TestTrue(TEXT("Prepare ordinary delivery"),PrepareDelivery(C,Delivery)))return false;
    TestTrue(TEXT("Ordinary resource batch publishes and releases its reservation"),Gate->Publish(*Gate->GetReceiver())&&!Gate->Reservation().IsValid());
    TestTrue(TEXT("Ordinary resource values match the committed effect"),C->Health()==60&&C->Mana()==50&&C->Stamina()==40);
    C->SetVitals(45,35,25);
    TestTrue(TEXT("Already-published replay cannot restore old After values"),Gate->Publish(*Gate->GetReceiver())&&C->Health()==45&&C->Mana()==35&&C->Stamina()==25);
    return true;
}
#endif
