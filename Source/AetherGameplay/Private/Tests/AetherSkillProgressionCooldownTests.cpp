#include "Misc/AutomationTest.h"
#include "Skills/AetherSkillProgressionContext.h"
#include "Commands/AetherProfileCommand.h"
#include "Combat/AetherCombat.h"
#include "Framework/AetherProgression.h"
#include "Effects/AetherBuffRuntime.h"
#include "Definitions/AetherV10Definitions.h"
#include "Profile/AetherProfileCodec.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherProgressionCooldownSourceTest,"Aether.V10.Skills.ProgressionReadsAuthoritativeDeadlines",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherProgressionCooldownSourceTest::RunTest(const FString&)
{
    auto* World=UWorld::CreateWorld(EWorldType::Game,false);
    if(!TestNotNull(TEXT("Isolated world"),World))return false;
    ON_SCOPE_EXIT { World->EndPlay(EEndPlayReason::Quit);World->DestroyWorld(false); };
    auto* Character=World->SpawnActor<AAetherCharacter>();
    auto* PlayerState=World->SpawnActor<AAetherPlayerState>();
    if(!TestNotNull(TEXT("Character"),Character)||!TestNotNull(TEXT("PlayerState"),PlayerState))return false;
    FAetherSkillRuleContext Context;Context.CharacterLevel=7;Context.bInCombat=true;Context.bAtResetService=true;
    AetherSkillProgression::ResolveExecutionState(*Character,0,Context);
    TestTrue(TEXT("Missing owner fails closed"),Context.bCoolingDown);
    Character->SetPlayerState(PlayerState);
    const double Now=Character->CombatTime();
    const auto* Haste=FAetherSkillDefinitionsV10::Get().Effect(TEXT("Body.Haste"),1);
    if(!TestNotNull(TEXT("Production long-cooldown skill"),Haste))return false;
    TestTrue(TEXT("Production cooldown outlasts recovery"),Haste->SkillCooldown>Haste->RecoverySeconds);
    PlayerState->CommitCooldown(TEXT("Body.Haste"),Haste->CooldownGroup,Haste->SkillCooldown,Haste->Cooldown,Now);
    Character->CastLockUntil=float(Now); // Recovery already finished; independent skill deadline remains.
    AetherSkillProgression::ResolveExecutionState(*Character,0,Context);
    TestTrue(TEXT("Cooldown remains after recovery"),Context.bCoolingDown&&!Context.bCasting);
    TestTrue(TEXT("Resolver preserves combat, level and service decisions"),Context.bInCombat&&Context.bAtResetService&&Context.CharacterLevel==7);
    const auto SkillDeadline=PlayerState->SkillCooldowns[0].EndsAt;
    AetherSkillProgression::ResolveExecutionState(*Character,0,Context);
    TestEqual(TEXT("Read cannot restart or clear a cooldown"),PlayerState->SkillCooldowns[0].EndsAt,SkillDeadline);
    PlayerState->SkillCooldowns.RemoveAll([](const auto& Deadline){return Deadline.Key.StartsWith(TEXT("Skill."));});
    AetherSkillProgression::ResolveExecutionState(*Character,0,Context);
    TestTrue(TEXT("Group-only cooldown still blocks progression"),Context.bCoolingDown);
    for(auto& Deadline:PlayerState->SkillCooldowns)Deadline.EndsAt=Now;
    AetherSkillProgression::ResolveExecutionState(*Character,0,Context);
    TestFalse(TEXT("Deadline equality is expired without requiring profile publication"),Context.bCoolingDown);
    Character->CastLockUntil=float(Now+1);
    AetherSkillProgression::ResolveExecutionState(*Character,0,Context);
    TestTrue(TEXT("Recovery and cooldown are independent flags"),Context.bCasting&&!Context.bCoolingDown);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherProgressionCooldownPresentationTest,"Aether.V10.Skills.ProgressionUsesAlignedOwnerSnapshot",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherProgressionCooldownPresentationTest::RunTest(const FString&)
{
    auto* World=UWorld::CreateWorld(EWorldType::Game,false);
    if(!TestNotNull(TEXT("Isolated presentation world"),World))return false;
    ON_SCOPE_EXIT { World->EndPlay(EEndPlayReason::Quit);World->DestroyWorld(false); };
    auto* Character=World->SpawnActor<AAetherCharacter>();
    auto* PlayerState=World->SpawnActor<AAetherPlayerState>();
    auto* GameState=World->SpawnActor<AGameStateBase>();
    if(!TestNotNull(TEXT("Character"),Character)||!TestNotNull(TEXT("PlayerState"),PlayerState)||!TestNotNull(TEXT("GameState"),GameState))return false;
    Character->SetPlayerState(PlayerState);Character->SetRole(ROLE_AutonomousProxy);World->SetGameState(GameState);
    FAetherSkillRuleContext Context;
    AetherSkillProgression::ResolveExecutionState(*Character,7,Context);
    TestTrue(TEXT("Unready atomic snapshot cannot enable growth"),Context.bCoolingDown);
    auto& Snapshot=Character->BuffRuntime->Snapshot;
    Snapshot.bReady=true;Snapshot.LifeId=FGuid::NewGuid();Snapshot.ProfileRevision=7;Snapshot.GrantRevision=3;
    PlayerState->SkillGrants.ProfileRevision=7;PlayerState->SkillGrants.GrantRevision=3;
    const double Now=Character->CombatTime();
    FAetherSkillCooldownDeadline Unaligned;Unaligned.Key=TEXT("Skill.Body.Haste");Unaligned.EndsAt=Now+25;
    PlayerState->SkillCooldowns.Add(Unaligned);
    AetherSkillProgression::ResolveExecutionState(*Character,7,Context);
    TestFalse(TEXT("Client does not use separately replicated deadlines over aligned snapshot"),Context.bCoolingDown);
    Snapshot.Cooldowns.Add(TEXT("Skill.Body.Haste"),Now+25);
    AetherSkillProgression::ResolveExecutionState(*Character,7,Context);
    TestTrue(TEXT("Aligned server deadline blocks after cast recovery"),Context.bCoolingDown&&!Context.bCasting);
    Snapshot.Cooldowns[TEXT("Skill.Body.Haste")]=Now;
    AetherSkillProgression::ResolveExecutionState(*Character,7,Context);
    TestFalse(TEXT("Same profile and grant revisions allow natural deadline expiry"),Context.bCoolingDown);
    AetherSkillProgression::ResolveExecutionState(*Character,8,Context);
    TestTrue(TEXT("Profile mismatch fails closed"),Context.bCoolingDown);
    ++PlayerState->SkillGrants.GrantRevision;
    AetherSkillProgression::ResolveExecutionState(*Character,7,Context);
    TestTrue(TEXT("Grant mismatch fails closed"),Context.bCoolingDown);
    --PlayerState->SkillGrants.GrantRevision;Snapshot.LifeId.Invalidate();
    AetherSkillProgression::ResolveExecutionState(*Character,7,Context);
    TestTrue(TEXT("Missing life identity fails closed"),Context.bCoolingDown);
    Snapshot.LifeId=FGuid::NewGuid();World->SetGameState(nullptr);
    AetherSkillProgression::ResolveExecutionState(*Character,7,Context);
    TestTrue(TEXT("No synchronized server clock cannot enable growth"),Context.bCoolingDown);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherProgressionCooldownCommandTest,"Aether.V10.Skills.ProgressionCooldownBlocksLearnUpgradeAndReset",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherProgressionCooldownCommandTest::RunTest(const FString&)
{
    const auto& D=FAetherV10Definitions::Get();
    if(!TestTrue(TEXT("Production definitions loaded"),D.bValid))return false;
    auto* World=UWorld::CreateWorld(EWorldType::Game,false);
    if(!TestNotNull(TEXT("Isolated command world"),World))return false;
    ON_SCOPE_EXIT { World->EndPlay(EEndPlayReason::Quit);World->DestroyWorld(false); };
    auto* Character=World->SpawnActor<AAetherCharacter>();
    auto* PlayerState=World->SpawnActor<AAetherPlayerState>();
    if(!TestNotNull(TEXT("Character"),Character)||!TestNotNull(TEXT("PlayerState"),PlayerState))return false;
    Character->SetPlayerState(PlayerState);
    FAetherProfileStateV10 Profile;Profile.CharacterId=TEXT("CooldownOwner");Profile.Claims={TEXT("Q_Main_02")};
    Profile.Skills.GrantStory(TEXT("Fire.Ignite"),TEXT("Story.Training"),D.Skills);
    Profile.Skills.AwardPoints(TEXT("Test.Points"),10,D.Skills);
    FAetherProfileCommandContext Context;Context.bCanManageInventory=true;Context.Skill.bAtResetService=true;
    Context.Skill.CompletedQuests.Add(TEXT("Q_Main_02"));
    if(!TestTrue(TEXT("Seed paid skill"),Profile.Skills.LearnNext(TEXT("Body.Haste"),FGuid::NewGuid(),Context.Skill,D.Skills).Code==EAetherSkillMutationCode::Applied))return false;
    FString Reason;TArray<uint8> Before;
    if(!TestTrue(*Reason,AetherProfileCodec::Encode(Profile,D.Items,D.Skills,D.Rules,Before,Reason)))return false;
    const double Now=Character->CombatTime();const auto* Haste=D.Skills.Effect(TEXT("Body.Haste"),1);
    if(!TestNotNull(TEXT("Haste definition"),Haste))return false;
    PlayerState->CommitCooldown(TEXT("Body.Haste"),Haste->CooldownGroup,Haste->SkillCooldown,Haste->Cooldown,Now);
    Character->CastLockUntil=float(Now);
    const EAetherCommandType Types[]={EAetherCommandType::LearnSkill,EAetherCommandType::UpgradeSkill,EAetherCommandType::ResetSkills};
    const FString Skills[]={TEXT("Body.Mend"),TEXT("Fire.Ignite"),TEXT("Body.Haste")};
    for(int32 Index=0;Index<UE_ARRAY_COUNT(Types);++Index)
    {
        AetherSkillProgression::ResolveExecutionState(*Character,Profile.Revision,Context.Skill);
        FAetherPlayerCommand Command;Command.Type=Types[Index];Command.SkillId=Skills[Index];
        Command.CommandId=AetherTransactions::NewCommandId(Profile.Revision);Command.ExpectedProfileRevision=Profile.Revision;
        FAetherTransaction Transaction;Transaction.ActorId=TEXT("Sentinel");FAetherCommandResult Result;
        TestFalse(TEXT("Live cooldown rejects growth candidate"),AetherProfileCommands::Prepare(Command,Profile.CharacterId,Profile,Context,D.Items,D.Skills,D.Rules,Transaction,Result,D.Economy));
        TestTrue(TEXT("Rejection is explicit and publishes no transaction"),Result.Code==EAetherCommandCode::NotReady&&Transaction.ActorId==TEXT("Sentinel")&&Transaction.Writes.IsEmpty());
        TestTrue(TEXT("Rejected growth reports no speculative changes"),Result.AffectedIds.IsEmpty()&&Result.AffectedDefinitionIds.IsEmpty()&&Result.ReasonParameters.IsEmpty());
        // Expire the same authoritative rows; no profile/grant revision bump or new timer is necessary.
        const auto Deadlines=PlayerState->SkillCooldowns;
        for(auto& Deadline:PlayerState->SkillCooldowns)Deadline.EndsAt=Now;
        AetherSkillProgression::ResolveExecutionState(*Character,Profile.Revision,Context.Skill);
        if(!TestTrue(TEXT("Same valid command can prepare after expiry"),AetherProfileCommands::Prepare(Command,Profile.CharacterId,Profile,Context,D.Items,D.Skills,D.Rules,Transaction,Result,D.Economy)))return false;
        FAetherProfileStateV10 Candidate;
        if(!TestTrue(TEXT("Candidate is decodable committed-shape profile"),AetherProfileCodec::Decode(Transaction.Writes[0].Value.Payload,D.Items,D.Skills,D.Rules,Candidate,Reason)))return false;
        TestEqual(TEXT("Exactly one candidate revision"),Candidate.Revision,Profile.Revision+1);
        TestEqual(TEXT("Expected learned, upgraded or reset rank"),Candidate.Skills.PermanentRank(Skills[Index]),Index==0?1:Index==1?2:0);
        TestEqual(TEXT("Exact point debit or historical refund"),Candidate.Skills.AvailableSkillPoints,Profile.Skills.AvailableSkillPoints+(Index==2?1:-1));
        PlayerState->SkillCooldowns=Deadlines;
    }
    TArray<uint8> After;AetherProfileCodec::Encode(Profile,D.Items,D.Skills,D.Rules,After,Reason);
    TestTrue(TEXT("Neither rejected nor prepared commands mutate authoritative input"),Before==After);
    TestTrue(TEXT("Growth operations never clear cooldown rows"),PlayerState->CooldownRemaining(TEXT("Body.Haste"),Haste->CooldownGroup,Now)>0);
    return true;
}
#endif
