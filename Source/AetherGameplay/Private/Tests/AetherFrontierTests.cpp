#include "Misc/AutomationTest.h"
#include "Framework/AetherProgression.h"
#include "ReactiveSimulation.h"
#if WITH_DEV_AUTOMATION_TESTS
namespace {constexpr auto V4Flags=EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter;}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV4Inventory,"Reactive.Frontier.InventoryAtomicCapacityAndOwnership",V4Flags)
bool FV4Inventory::RunTest(const FString&)
{
    FAetherProfile P;P.CharacterId="Test";TestTrue(TEXT("Add stack"),P.Add("Potion",35));TestEqual(TEXT("Two stacks"),P.Inventory.Num(),2);
    const auto First=P.Inventory[0].InstanceId;TestTrue(TEXT("Split"),P.Split(First,5));TestEqual(TEXT("Split conserves"),P.Count("Potion"),35);
    TestTrue(TEXT("Merge"),P.Merge(P.Inventory.Last().InstanceId,First));TestEqual(TEXT("Merge conserves"),P.Count("Potion"),35);
    TestFalse(TEXT("Reject negative quantity"),P.Remove("Potion",-1));TestFalse(TEXT("Reject invented item"),P.Add("Unknown",1));
    TestTrue(TEXT("Sword"),P.Add("TrainingSword",1));auto Sword=P.Inventory.Last().InstanceId;TestTrue(TEXT("Owned equip"),P.Equip(Sword));
    TestFalse(TEXT("No foreign instance"),P.Equip(FGuid::NewGuid()));TestFalse(TEXT("Cannot remove equipped item"),P.Remove("TrainingSword",1));
    P.Add("TrainingHammer",1);P.Equip(P.Inventory.Last().InstanceId);P.Add("TrainingShield",1);TestFalse(TEXT("Two hands disallow shield"),P.Equip(P.Inventory.Last().InstanceId));
    while(P.Inventory.Num()<32)P.Add("TrainingSword",1);const int32 Before=P.Count("Potion");
    TestFalse(TEXT("Capacity failure"),P.Add("Potion",1000));TestEqual(TEXT("Capacity rollback"),P.Count("Potion"),Before);TestTrue(TEXT("Valid profile"),P.Validate());return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV4Quest,"Reactive.Frontier.PersonalQuestsAndRewardIdempotency",V4Flags)
bool FV4Quest::RunTest(const FString&)
{
    FAetherProfile A,B;A.CharacterId="A";B.CharacterId="B";
    TestFalse(TEXT("No skipping prerequisites"),A.Observe("SupplyRestored"));
    for(int32 Q=0;Q<3;++Q){for(auto F:FAetherProfile::Objectives(FAetherProfile::QuestId(Q)))A.Observe(F);TestTrue(TEXT("Claim current quest"),A.Claim(FAetherProfile::QuestId(Q)));TestFalse(TEXT("Duplicate reward rejected"),A.Claim(FAetherProfile::QuestId(Q)));}
    TestTrue(TEXT("Both field quests available"),A.Available("Q_Main_04")&&A.Available("Q_Main_05"));A.Observe("SupplyRestored");A.Claim("Q_Main_05");
    TestFalse(TEXT("Both field quests required"),A.Available("Q_Main_06"));TestTrue(TEXT("Other player independent"),B.Claims.IsEmpty()&&B.Evidence.IsEmpty());
    for(auto F:FAetherProfile::Objectives("Q_Main_04"))A.Observe(F);A.Claim("Q_Main_04");TestTrue(TEXT("Converged prerequisites"),A.Available("Q_Main_06"));
    TestFalse(TEXT("Repeated fire doesn't count"),A.Observe("ForestFire0"));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV4Rain,"Reactive.Frontier.RainCannotCreateElectricalWetnessOrEdges",V4Flags)
bool FV4Rain::RunTest(const FString&)
{
    Reactive::FSimulation Dry,Rain;Reactive::FEnvironment Weather;Weather.RainKgPerM2Sec=1;Rain.SetEnvironment(Weather);
    auto Wood=Reactive::FMaterial::Wood();auto Metal=Reactive::FMaterial::Metal();
    auto D=Dry.Register(Wood,FVector::ZeroVector,50),R=Rain.Register(Wood,FVector::ZeroVector,50);
    auto DM=Dry.Register(Metal,FVector(90,0,0),50),RM=Rain.Register(Metal,FVector(90,0,0),50);
    for(int32 I=0;I<30;++I){Dry.Step();Rain.Step();}Dry.DrainEvents();Rain.DrainEvents();
    TestTrue(TEXT("Rain wets thermal state"),Rain.Find(R)->WaterKg>Dry.Find(D)->WaterKg);
    TestEqual(TEXT("Rain does not electrically wet"),Rain.Find(R)->ElectricalWetness01,0.0);
    Reactive::FStimulus E;E.Target=D;E.ElectricalJ=100;Dry.Enqueue(E);E.Target=R;Rain.Enqueue(E);Dry.Step();Rain.Step();
    int32 DS=0,RS=0;for(auto Ev:Dry.DrainEvents())if(Ev.Kind==Reactive::EEvent::Shock&&Ev.Body==DM)++DS;
    for(auto Ev:Rain.DrainEvents())if(Ev.Kind==Reactive::EEvent::Shock&&Ev.Body==RM)++RS;
    TestEqual(TEXT("No rain-created wood conduction"),RS,DS);TestEqual(TEXT("Dry wood blocks propagation"),RS,0);
    Reactive::FStimulus Splash;Splash.Target=R;Splash.WaterKg=.1;
    Reactive::FSimulation Fresh;auto F=Fresh.Register(Wood,FVector::ZeroVector,50);Splash.Target=F;Fresh.Enqueue(Splash);Fresh.Step();
    TestTrue(TEXT("Splash has separate electrical channel"),Fresh.Find(F)->ElectricalWetness01>0);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV4Contact,"Reactive.Frontier.ElectricalContactIndependentOfHeatVisibility",V4Flags)
bool FV4Contact::RunTest(const FString&)
{
    Reactive::FSimulation S;auto M=Reactive::FMaterial::Metal();auto A=S.Register(M,FVector::ZeroVector,60);auto B=S.Register(M,FVector(80,0,0),60);
    S.CanExchange=[](uint32,uint32){return true;};S.CanConduct=[](uint32,uint32){return false;};
    Reactive::FStimulus E;E.Target=A;E.ElectricalJ=100;S.Enqueue(E);S.Step();int32 Count=0;for(auto Ev:S.DrainEvents())if(Ev.Body==B&&Ev.Kind==Reactive::EEvent::Shock)++Count;
    TestEqual(TEXT("Visibility alone is not electrical contact"),Count,0);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV4WetDecay,"Reactive.Frontier.SplashOnRainSoakedBodyStillDecays",V4Flags)
bool FV4WetDecay::RunTest(const FString&)
{
    Reactive::FSimulation S;auto M=Reactive::FMaterial::Wood();auto A=S.Register(M,FVector::ZeroVector,40,20,M.WaterCapacityKg);
    Reactive::FStimulus Splash;Splash.Target=A;Splash.WaterKg=.5;S.Enqueue(Splash);S.Step();
    TestTrue(TEXT("Saturated body still receives electrical splash channel"),S.Find(A)->ElectricalWetness01>.9);
    for(int32 I=0;I<300;++I)S.Step();TestEqual(TEXT("Channel decays even near ambient and otherwise idle"),S.Find(A)->ElectricalWetness01,0.0);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV4Distance,"Reactive.Frontier.ElectricalDistanceLossAndPulseLifetime",V4Flags)
bool FV4Distance::RunTest(const FString&)
{
    auto Dose=[](double Distance)
    {
        Reactive::FSimulation S;auto M=Reactive::FMaterial::Metal();auto A=S.Register(M,FVector::ZeroVector,200);auto B=S.Register(M,FVector(Distance,0,0),200);
        Reactive::FStimulus E;E.Target=A;E.ElectricalJ=1000;S.Enqueue(E);S.Step();double Received=0;
        for(auto Ev:S.DrainEvents())if(Ev.Body==B&&Ev.Kind==Reactive::EEvent::Shock)Received+=Ev.Magnitude;
        return Received;
    };
    TestTrue(TEXT("A longer sole path reduces delivery without renormalizing the loss"),Dose(300)<Dose(100));
    Reactive::FSettings Limits;Limits.MaxElectricalNodes=1;Reactive::FSimulation S(Limits);auto M=Reactive::FMaterial::Metal();auto A=S.Register(M,FVector::ZeroVector,60);S.Register(M,FVector(100,0,0),60);
    Reactive::FStimulus E;E.Target=A;E.ElectricalJ=1000;S.Enqueue(E);S.Step();
    TestEqual(TEXT("Budget rejection deposits no energy into last node"),S.GetStats().ElectricalDepositedJ,0.0);TestEqual(TEXT("Budget loss accounted"),S.GetStats().ElectricalLostJ,1000.0);
    S.DrainEvents();S.Step();int32 Shocks=0;for(auto Ev:S.DrainEvents())if(Ev.Kind==Reactive::EEvent::Shock)++Shocks;
    TestEqual(TEXT("No delayed or stored shock after the pulse"),Shocks,0);return true;
}
#endif
