#include "Misc/AutomationTest.h"
#include "Effects/AetherBuffState.h"
#include "Effects/AetherEffectEvent.h"
#include "Actions/AetherActionPolicy.h"
#include "Definitions/AetherV10Definitions.h"
#include "Contracts/AetherPlayerCommand.h"
#include "Contracts/AetherTransaction.h"
#include "Inventory/AetherConsumableEffect.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherSystemsBuffRules,"Aether.Systems.Buff.LifecycleSourcesAndImmunity",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherSystemsBuffRules::RunTest(const FString&)
{
    const auto& Catalog=FAetherBuffDefinitions::Get();if(!TestTrue(*Catalog.Error,Catalog.bValid))return false;
    FAetherBuffState S;S.LifeId=FGuid::NewGuid();const FGuid Delivery=FGuid::NewGuid();
    auto Haste=Catalog.Buffs.FindChecked(TEXT("Sample.Haste"));
    TestTrue(TEXT("First effect applies"),S.Apply(Haste,TEXT("Potion"),0,Delivery)==EAetherBuffResult::Applied);
    const auto Instance=S.Instances[0].InstanceId;
    TestTrue(TEXT("Delivery retry does not refresh deadline"),S.Apply(Haste,TEXT("Potion"),5,Delivery)==EAetherBuffResult::Replayed&&S.Instances[0].ExpiresAt==20);
    TestTrue(TEXT("Normal refresh preserves instance"),S.Apply(Haste,TEXT("Potion"),5)==EAetherBuffResult::Refreshed&&S.Instances[0].InstanceId==Instance&&S.Instances[0].ExpiresAt==25);
    auto Strong=Haste;Strong.Id=TEXT("Test.Strong");Strong.Priority=2;Strong.Duration=5;
    S.Apply(Strong,TEXT("Skill"),5);
    TestTrue(TEXT("Weak source is retained but suppressed"),S.Instances[0].bSuppressed);
    FAetherBuffDueEvent Event;TestTrue(TEXT("Strong effect expires first"),S.NextDue(10,Event)&&Event.bExpiry);
    TestFalse(TEXT("Weak effect resumes after strong expiry"),S.Instances[0].bSuppressed);
    S.Apply(Haste,TEXT("Equipment"),10);S.RemoveSource(TEXT("Potion"));
    TestEqual(TEXT("Removing one source preserves another"),S.Instances.Num(),1);
    S.Apply(Catalog.Buffs.FindChecked(TEXT("Sample.Antitoxin")),TEXT("Tonic"),10);
    TestTrue(TEXT("Immunity rejects new poison"),S.Apply(Catalog.Buffs.FindChecked(TEXT("Sample.Poison")),TEXT("Snake"),10)==EAetherBuffResult::Immune);
    S.RemoveSource(TEXT("Tonic"));auto Poison=Catalog.Buffs.FindChecked(TEXT("Sample.Poison"));
    for(int32 I=0;I<6;++I)S.Apply(Poison,TEXT("Snake"),10+I);
    const auto* P=S.Instances.FindByPredicate([](const auto& I){return I.Definition.Id==TEXT("Sample.Poison");});
    TestTrue(TEXT("Overflow refresh caps stacks"),P&&P->Stacks()==3&&P->ExpiresAt==27);
    TestTrue(TEXT("Negative dispel succeeds"),S.Dispel(TEXT("Negative")));
    TestEqual(TEXT("Dispel does not remove positive effect"),S.Instances.Num(),1);
    Haste.Reapply=EAetherBuffReapply::AddStack;Haste.bPerSource=false;Haste.MaxStacks=2;Haste.ExclusiveGroup.Reset();
    S={};S.LifeId=FGuid::NewGuid();S.Apply(Haste,TEXT("A"),0);S.Apply(Haste,TEXT("B"),0);
    Haste.Overflow=EAetherBuffOverflow::Reject;
    TestTrue(TEXT("Reject overflow changes no lifetime"),S.Apply(Haste,TEXT("C"),2)==EAetherBuffResult::Capacity&&S.Instances[0].ExpiresAt==20);
    Haste.Overflow=EAetherBuffOverflow::ReplaceSourceStack;S.Apply(Haste,TEXT("C"),2);
    TestTrue(TEXT("Explicit overflow transfers exactly one source stack"),S.Instances[0].Stacks()==2&&!S.Instances[0].Sources.Contains(TEXT("A"))&&S.Instances[0].Sources.Contains(TEXT("C")));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherSystemsPeriodRules,"Aether.Systems.Buff.FrameRateAndExpiry",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherSystemsPeriodRules::RunTest(const FString&)
{
    const auto Definition=FAetherBuffDefinitions::Get().Buffs.FindChecked(TEXT("Sample.Regeneration"));
    for(int32 FPS:{30,60,120})
    {
        FAetherBuffState S;S.LifeId=FGuid::NewGuid();S.Apply(Definition,TEXT("Test"),0);
        int32 Ticks=0,Expiries=0;double Total=0;uint64 LastIndex=0;FAetherBuffDueEvent E;
        for(int32 Frame=1;Frame<=FPS*11;++Frame)while(S.NextDue(double(Frame)/FPS,E))
        {
            if(E.bExpiry){++Expiries;continue;}++Ticks;TestTrue(TEXT("Tick identity increases exactly once"),E.TickIndex==++LastIndex);
            for(const auto& O:E.Operations)if(O.Kind==EAetherBuffOperation::Heal)Total+=O.Value*E.Stacks;
        }
        TestEqual(TEXT("Four periods strictly before expiry"),Ticks,4);TestEqual(TEXT("Exactly one expiry"),Expiries,1);
        TestEqual(TEXT("Frame rate independent total"),Total,16.);TestTrue(TEXT("No remaining instances"),S.Instances.IsEmpty());
    }
    FAetherBuffState S;S.LifeId=FGuid::NewGuid();S.Apply(Definition,TEXT("Test"),0);FAetherBuffDueEvent E;int32 Catchup=0;
    while(S.NextDue(100,E))++Catchup;TestEqual(TEXT("Large frame catches all periods and expiry"),Catchup,5);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherSystemsProjectionRules,"Aether.Systems.Attributes.PolicyAndIntervals",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherSystemsProjectionRules::RunTest(const FString&)
{
    FString Why;FAetherResolvedAttributes Out;
    TArray<FAetherAttributeContribution> Inputs={{TEXT("Armor"),TEXT("Equipment"),EAetherAttributeOperation::Add,8},{TEXT("Armor"),TEXT("Weakness"),EAetherAttributeOperation::Add,-20},
        {TEXT("MoveSpeed"),TEXT("Haste"),EAetherAttributeOperation::Percent,.2}};
    TestTrue(TEXT("Legal negative contributions resolve"),AetherAttributes::Resolve(Inputs,Out,Why));
    TestEqual(TEXT("Armor cannot become negative"),Out.Values.FindRef(TEXT("Armor")),0.);
    TestTrue(TEXT("Percent applies to movement baseline"),FMath::IsNearlyEqual(Out.Values.FindRef(TEXT("MoveSpeed")),1.2));
    Inputs.Add({TEXT("Health"),TEXT("InvalidPersistentHealth"),EAetherAttributeOperation::Add,10});
    TestFalse(TEXT("Current pools cannot be continuous modifiers"),AetherAttributes::Resolve(Inputs,Out,Why));
    FAetherResourceAdvanceInterval A,B;A.Identity.LifeId=FGuid::NewGuid();A.Start=0;A.End=1;B=A;B.Start=1;B.End=2;
    TestTrue(TEXT("Adjacent same-context intervals merge"),A.CanMerge(B));B.Identity.StateRevision=1;
    TestFalse(TEXT("Projection boundary stops merge"),A.CanMerge(B));B.Identity.StateRevision=0;B.HazardRate=2;
    TestFalse(TEXT("Hazard boundary stops merge"),A.CanMerge(B));B=A;B.Start=1;B.End=31;
    TestFalse(TEXT("Interval has bounded extent"),A.CanMerge(B));
    FAetherActionContext C;C.bAvatar=C.bAlive=true;C.bSilenced=true;
    TestTrue(TEXT("Silence denies spells"),AetherActionPolicy::Query(EAetherActionKind::Spell,C)==EAetherActionDenial::Silenced);
    TestTrue(TEXT("Silence preserves melee and item actions"),AetherActionPolicy::Query(EAetherActionKind::Melee,C)==EAetherActionDenial::None&&AetherActionPolicy::Query(EAetherActionKind::Inventory,C)==EAetherActionDenial::None);
    C.bStorage=true;TestTrue(TEXT("Storage barrier also blocks movement"),AetherActionPolicy::Query(EAetherActionKind::Move,C)==EAetherActionDenial::Storage);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherSystemsWireRules,"Aether.Systems.Protocol.UnbindAndBuffDelivery",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherSystemsWireRules::RunTest(const FString&)
{
    FAetherPlayerCommand C;C.ProtocolVersion=5;C.Type=EAetherCommandType::UnbindSkill;C.ExpectedProfileRevision=0;C.CommandId=AetherTransactions::NewCommandId(0);C.SlotId=TEXT("Hotbar.1");
    FString Why;TArray<uint8> Bytes,Again;FAetherPlayerCommand Decoded;
    TestTrue(TEXT("V5 unbind encodes"),AetherCommands::Encode(C,Bytes,Why));
    TestTrue(TEXT("V5 canonical bytes roundtrip"),AetherCommands::Decode(Bytes,Decoded,Why)&&AetherCommands::Encode(Decoded,Again,Why)&&Again==Bytes);
    C.ProtocolVersion=4;TestFalse(TEXT("V4 cannot carry appended action"),AetherCommands::Encode(C,Again,Why));
    C.ProtocolVersion=5;C.SlotId=TEXT("Hotbar.5");TestFalse(TEXT("Unbind rejects invalid slot"),AetherCommands::Encode(C,Again,Why));
    FAetherConsumableEffectV10 E;E.DeliveryId=AetherTransactions::NewCommandId(0);E.ItemInstanceId=FGuid::NewGuid();E.DefinitionId=TEXT("HastePotion");E.ProfileRevision=1;
    E.Before.LifeId=FGuid::NewGuid();E.After=E.Before;E.After.Revision=1;E.After.UseReadyAtUnixMs=1700000003000LL;
    E.BuffId=TEXT("Sample.Haste");E.BuffRevision=1;E.BuffExpiresAtUnixMs=1700000020000LL;
    TestTrue(TEXT("Buff-only effect needs no fake health increase"),AetherConsumableEffects::Encode(E,Bytes));
    FAetherConsumableEffectV10 Copy;
    TestTrue(TEXT("Fixed delivery identity and absolute expiry survive wire"),AetherConsumableEffects::Decode(Bytes,Copy)&&Copy.DeliveryId==E.DeliveryId&&Copy.BuffExpiresAtUnixMs==E.BuffExpiresAtUnixMs);
    TestEqual(TEXT("Buff payload uses version two"),int32(Bytes.IsValidIndex(4)?Bytes[4]:0),2);
    E.BuffId.Reset();E.BuffRevision=0;E.BuffExpiresAtUnixMs=0;E.Before.Health=50;E.After.Health=60;
    TestTrue(TEXT("Legacy resource-only payload stays version one"),AetherConsumableEffects::Encode(E,Bytes)&&Bytes[4]==1&&AetherConsumableEffects::Decode(Bytes,Copy));
    const auto& D=FAetherV10Definitions::Get();TestTrue(*D.Error,D.bValid);
    TestTrue(TEXT("Authored arc is real item content"),D.Items.Items.FindChecked(TEXT("BrittleSword")).AttackTrajectory.Num()>=2);
    return true;
}
#endif
