#include "Misc/AutomationTest.h"
#include "Quests/AetherGuide.h"
#include "Characters/AetherFrontierCharacter.h"
#include "World/AetherFrontierProp.h"
#include "World/AetherEncounters.h"
#include "Definitions/AetherV10Definitions.h"
#include "Definitions/AetherWorldDefinition.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherCurrentGuidanceTest,"Aether.Systems.Guidance.CurrentSnapshotAndOwnedTargets",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherCurrentGuidanceTest::RunTest(const FString&)
{
    const auto& D=FAetherV10Definitions::Get();if(!TestTrue(*D.Error,D.bValid))return false;
    auto* W=UWorld::CreateWorld(EWorldType::Game,false);if(!W)return false;
    ON_SCOPE_EXIT {W->EndPlay(EEndPlayReason::Quit);W->DestroyWorld(false);};
    FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* C=W->SpawnActor<AAetherFrontierCharacter>(FVector::ZeroVector,FRotator::ZeroRotator,Params);if(!C)return false;
    FAetherProfileStateV10 P;P.CharacterId=TEXT("GuidanceFixture");P.Revision=42;
    TestTrue(TEXT("Missing snapshot is loading, never completion"),!AetherGuide::Resolve(C,nullptr).bReady&&!AetherGuide::Resolve(C,nullptr).bHasTarget);
    TestEqual(TEXT("Locked preference selects current available quest"),AetherGuide::SelectQuest(P,TEXT("Q_Main_08")),FName(TEXT("Q_Main_01")));
    P.Claims={TEXT("Q_Main_01"),TEXT("Q_Main_02"),TEXT("Q_Main_03")};
    TestTrue(TEXT("Independent branches cycle via current progression rules"),AetherGuide::SelectQuest(P,TEXT("Q_Main_04"),true)==TEXT("Q_Main_05")&&AetherGuide::SelectQuest(P,TEXT("Q_Main_05"),true)==TEXT("Q_Main_04"));
    C->TrackedQuest=TEXT("Q_Main_04");auto G=AetherGuide::Resolve(C,&P);
    TestTrue(TEXT("Unloaded actor keeps canonical persistent position and snapshot revision"),G.bHasTarget&&G.ProfileRevision==42&&G.Position.Equals(D.Rules.Objectives.FindChecked(TEXT("ForestFire0")).Position));
    P.Claims.Add(TEXT("Q_Main_04"));
    FAetherPendingRewardV10 Pending;Pending.RewardId=FGuid::NewGuid();Pending.SourceId=TEXT("Q_Main_04");P.PendingRewards.Add(Pending);
    G=AetherGuide::Resolve(C,&P);
    TestTrue(TEXT("Deferred reward does not hold back claimed quest navigation"),G.Quest==TEXT("Q_Main_05")&&G.Hint.Contains(D.Guidance.PendingRewardHint));
    P.PendingRewards.Reset();P.Claims={TEXT("Q_Main_01"),TEXT("Q_Main_02")};P.Evidence={TEXT("Melee1"),TEXT("Melee2"),TEXT("Melee3"),TEXT("Block")};C->TrackedQuest=TEXT("Q_Main_03");
    G=AetherGuide::Resolve(C,&P);
    const auto Teacher=FAetherWorldDefinitions::Get().Find(TEXT("Teacher"))->Location;
    TestTrue(TEXT("Missing permanent skills point to teacher"),G.Position.Equals(Teacher));
    P.Skills.GrantStory(TEXT("Fire.Ignite"),TEXT("Fixture.Fire"),D.Skills);P.Skills.GrantStory(TEXT("Water.Draw"),TEXT("Fixture.Water"),D.Skills);
    auto* Fire=W->SpawnActor<AAetherFrontierProp>(FVector(900,600,70),FRotator::ZeroRotator,Params);auto* Other=W->SpawnActor<AActor>();
    if(!Fire||!Other)return false;Fire->Service=TEXT("TrainingExtinguished");Fire->SetOwner(Other);
    TestTrue(TEXT("Another owner's private fire cannot satisfy preparation"),AetherGuide::Resolve(C,&P).Position.Equals(Teacher));
    Fire->SetOwner(C);G=AetherGuide::Resolve(C,&P);
    TestTrue(TEXT("Own dynamic target uses actual location"),G.Objective==TEXT("TrainingExtinguished")&&G.Position.Equals(Fire->GetActorLocation()));
    P.Evidence.Add(TEXT("TrainingExtinguished"));Fire->Destroy();
    TestTrue(TEXT("Finished objectives never demand recreated private actors"),AetherGuide::Resolve(C,&P).bRewardReady);
    P.Claims={TEXT("Q_Main_01"),TEXT("Q_Main_02"),TEXT("Q_Main_03"),TEXT("Q_Main_04"),TEXT("Q_Main_05"),TEXT("Q_Main_06")};P.Evidence.Reset();C->TrackedQuest=TEXT("Q_Main_07");
    auto* Director=W->SpawnActor<AAetherEncounterDirector>();if(!Director)return false;Director->Abbey.Phase=EAetherEncounterPhase::Boss;Director->Abbey.Participants={TEXT("Other")};
    const FString BaseLabel=AetherGuide::Resolve(C,&P).Label;
    Director->Abbey.Participants={P.CharacterId};G=AetherGuide::Resolve(C,&P);
    TestTrue(TEXT("Only participants receive stage data from canonical encounter center"),G.Label!=BaseLabel&&G.Position.Equals(D.Rules.Encounters.FindChecked(TEXT("AbbeyBoss")).Center));
    for(auto Phase:{EAetherEncounterPhase::Idle,EAetherEncounterPhase::Failed,EAetherEncounterPhase::Succeeded}){Director->Abbey.Phase=Phase;TestEqual(TEXT("Inactive phases preserve base objective"),AetherGuide::Resolve(C,&P).Label,BaseLabel);}
    TestEqual(TEXT("Read-only navigation does not mutate persistent revision"),P.Revision,int64(42));
    for(const auto& Q:D.Rules.Quests)P.Claims.AddUnique(Q.Id.ToString());
    G=AetherGuide::Resolve(C,&P);TestTrue(TEXT("Completion requires all claims and unlocked daily anchor"),G.Quest.IsNone()&&G.Title==D.Guidance.Completion.Title&&G.bHasTarget);
    return true;
}
#endif
