#include "Misc/AutomationTest.h"
#include "Skills/AetherSkillCooldownState.h"
#include "Skills/AetherSkillDefinitions.h"
#include "Skills/AetherSkillAbilityBinding.h"
#include "Characters/AetherFrontierCharacter.h"
#include "Framework/AetherProgression.h"
#include "AIController.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherNpcSkillCooldownTest,"Aether.Skills.DefinitionAscOwnsCooldowns",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherNpcSkillCooldownTest::RunTest(const FString&)
{
    auto* World=UWorld::CreateWorld(EWorldType::Game,false);
    if(!TestNotNull(TEXT("Isolated authority world"),World))return false;
    ON_SCOPE_EXIT {World->EndPlay(EEndPlayReason::Quit);World->DestroyWorld(false);};
    const auto Spawn=[&]()
    {
        FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* C=World->SpawnActor<AAetherFrontierCharacter>(FVector::ZeroVector,FRotator::ZeroRotator,Params);
        if(!C)return C;
        C->SetActorEnableCollision(false);C->Fighter=EAetherFighter::FireCaster;
        C->SkillAuthority=EAetherSkillAuthority::Definition;C->SkillLoadoutId=TEXT("FireCaster");
        C->AbilitySystem->AddAttributeSetSubobject(C->Attributes.Get());C->AbilitySystem->InitAbilityActorInfo(C,C);
        C->SetVitals(C->MaxHealth,100,100);C->GrantSpells();return C;
    };
    auto* Npc=Spawn();auto* Controller=World->SpawnActor<AAIController>();
    if(!TestNotNull(TEXT("Definition NPC"),Npc)||!TestNotNull(TEXT("AI controller"),Controller))return false;
    Controller->Possess(Npc);
    auto* System=Cast<UAetherDefinitionAbilitySystem>(Npc->AbilitySystem);
    if(!TestNotNull(TEXT("Production NPC owns the concrete cooldown ASC"),System))return false;
    const auto* Fire=FAetherSkillDefinitionsV10::Get().Effect(TEXT("Fire.Ignite"),1);
    const auto* Storm=FAetherSkillDefinitionsV10::Get().Effect(TEXT("Storm.Strike"),1);
    if(!TestNotNull(TEXT("Production fire effect"),Fire)||!TestNotNull(TEXT("Production storm effect"),Storm))return false;
    TestTrue(TEXT("Production elemental skills share a group"),Fire->CooldownGroup==Storm->CooldownGroup&&Fire->Cooldown>0);
    TestEqual(TEXT("Fresh definition ASC has no cooldown"),Npc->SkillCooldownRemaining(TEXT("Fire.Ignite")),0.f);
    TestTrue(TEXT("Production GAS starts a fire cast"),Npc->TrySkill(TEXT("Fire.Ignite")));
    TestEqual(TEXT("Successful GAS cast pays the production cost once"),Npc->Mana(),100.f-float(Fire->ManaCost));
    if(!TestTrue(TEXT("Successful ExecuteCast commits its ASC group deadline"),Npc->SkillCooldownRemaining(TEXT("Storm.Strike"))>0&&!System->SkillCooldowns.IsEmpty()))return false;
    const double FirstEnd=System->SkillCooldowns[0].EndsAt;
    Npc->CastLockUntil=0; // 隔离恢复时间与独立冷却；不改变权威冷却状态。
    TestFalse(TEXT("GAS rejects another group skill even after recovery is cleared"),Npc->TrySkill(TEXT("Storm.Strike")));
    TestEqual(TEXT("Rejected group cast cannot charge mana"),Npc->Mana(),100.f-float(Fire->ManaCost));
    TestEqual(TEXT("Rejected cast does not restart the deadline"),System->SkillCooldowns[0].EndsAt,FirstEnd);
    Npc->CancelActions();Npc->ResetCombat();
    TestEqual(TEXT("Cancel and combat reset cannot erase ASC cooldowns"),System->SkillCooldowns[0].EndsAt,FirstEnd);
    Controller->UnPossess();
    TestTrue(TEXT("Missing current avatar fails closed"),Npc->SkillCooldownRemaining(TEXT("Fire.Ignite"))>0);
    Controller->Possess(Npc);
    TestEqual(TEXT("Controller replacement preserves the same ASC deadline"),System->SkillCooldowns[0].EndsAt,FirstEnd);
    TestFalse(TEXT("Repossessing the same NPC cannot bypass GAS cooldown"),Npc->TrySkill(TEXT("Fire.Ignite")));
    Npc->SkillLoadoutId=TEXT("MeleeOnly");FString Why;
    TestTrue(TEXT("Remove definition grant through production publication"),Npc->GrantDefinitionSkills(Why));
    Npc->SkillLoadoutId=TEXT("FireCaster");
    TestTrue(TEXT("Restore definition grant through production publication"),Npc->GrantDefinitionSkills(Why));
    TestFalse(TEXT("Removing and regranting a spec cannot wash cooldown state"),Npc->TrySkill(TEXT("Fire.Ignite")));

    const double Now=Npc->CombatTime();
    // 显式长技能/组期限是合成边界数据；存储与查询仍使用生产 ASC 和同一公共期限算法。
    TestTrue(TEXT("Definition owner accepts finite independent skill and group deadlines"),System->CommitCooldown(TEXT("Fire.Ignite"),Fire->CooldownGroup,7,3,Now));
    TestTrue(TEXT("Shorter recommit is accepted without shortening"),System->CommitCooldown(TEXT("Fire.Ignite"),Fire->CooldownGroup,2,1,Now));
    TestEqual(TEXT("Same skill preserves the greatest deadline"),System->CooldownRemaining(TEXT("Fire.Ignite"),Fire->CooldownGroup,Now),7.);
    TestEqual(TEXT("Other skill observes the shared group deadline"),System->CooldownRemaining(TEXT("Storm.Strike"),Storm->CooldownGroup,Now),3.);
    TestEqual(TEXT("Independent skill survives group expiry"),System->CooldownRemaining(TEXT("Fire.Ignite"),Fire->CooldownGroup,Now+4),3.);
    TestEqual(TEXT("Exact skill deadline is expired"),System->CooldownRemaining(TEXT("Fire.Ignite"),Fire->CooldownGroup,Now+7),0.);
    const int32 Rows=System->SkillCooldowns.Num();
    TestFalse(TEXT("Invalid duration does not mutate shared deadline state"),System->CommitCooldown(TEXT("Fire.Ignite"),Fire->CooldownGroup,std::numeric_limits<double>::quiet_NaN(),1,Now));
    TestEqual(TEXT("Invalid commit leaves all rows intact"),System->SkillCooldowns.Num(),Rows);
    TestEqual(TEXT("Invalid commit preserves existing expiration"),System->CooldownRemaining(TEXT("Fire.Ignite"),Fire->CooldownGroup,Now),7.);
    for(auto& Deadline:System->SkillCooldowns)Deadline.EndsAt=Now;
    TestTrue(TEXT("GAS can cast again when authoritative deadlines expire"),Npc->TrySkill(TEXT("Fire.Ignite")));

    auto* Fresh=Spawn();if(!TestNotNull(TEXT("New NPC lifetime"),Fresh))return false;
    auto* FreshSystem=Cast<UAetherDefinitionAbilitySystem>(Fresh->AbilitySystem);
    if(!TestNotNull(TEXT("New lifetime has its own ASC"),FreshSystem))return false;
    TestEqual(TEXT("New NPC does not inherit a different ASC deadline"),Fresh->SkillCooldownRemaining(TEXT("Fire.Ignite")),0.f);
    // 空场景没有雷击目标，真实 GAS 激活会由生产 FindSkillTarget 拒绝。
    Fresh->TrySkill(TEXT("Storm.Strike"));
    TestTrue(TEXT("Failed target validation cannot commit cooldown"),FreshSystem->SkillCooldowns.IsEmpty());
    TestEqual(TEXT("Failed target validation cannot charge mana"),Fresh->Mana(),100.f);
    Fresh->SetVitals(Fresh->MaxHealth,0,100);
    TestFalse(TEXT("Production GAS cost rejects insufficient resources"),Fresh->TrySkill(TEXT("Fire.Ignite")));
    TestTrue(TEXT("Failed cost cannot commit a deadline"),FreshSystem->SkillCooldowns.IsEmpty());
    auto* WrongSystem=NewObject<UAbilitySystemComponent>(Fresh);
    WrongSystem->InitAbilityActorInfo(Fresh,Fresh);Fresh->AbilitySystem=WrongSystem;
    TestTrue(TEXT("Definition authority with the wrong ASC type fails closed"),Fresh->SkillCooldownRemaining(TEXT("Fire.Ignite"))>0);
    Fresh->AbilitySystem=FreshSystem;

    // Adventure 的正式 Definition 探针可由普通 PlayerState 控制，只有持久 Profile 所有者改走 PS 权威。
    auto* Probe=Spawn();auto* BasicPlayerState=World->SpawnActor<APlayerState>();
    if(!TestNotNull(TEXT("Definition probe"),Probe)||!TestNotNull(TEXT("Ordinary engine player state"),BasicPlayerState))return false;
    Probe->SetPlayerState(BasicPlayerState);
    TestEqual(TEXT("Non-profile PlayerState does not replace definition ASC authority"),Probe->SkillCooldownRemaining(TEXT("Fire.Ignite")),0.f);
    TestTrue(TEXT("Definition probe still executes its granted GAS skill"),Probe->TrySkill(TEXT("Fire.Ignite")));
    TestTrue(TEXT("Definition probe commits to its own ASC"),Probe->SkillCooldownRemaining(TEXT("Storm.Strike"))>0);

    auto* ProfilePawn=World->SpawnActor<AAetherFrontierCharacter>();
    auto* PlayerState=World->SpawnActor<AAetherPlayerState>();
    if(!TestNotNull(TEXT("Profile pawn"),ProfilePawn)||!TestNotNull(TEXT("Persistent owner"),PlayerState))return false;
    ProfilePawn->AbilitySystem->InitAbilityActorInfo(ProfilePawn,ProfilePawn);
    TestTrue(TEXT("Profile authority cannot fall back to the pawn's definition ASC"),ProfilePawn->SkillCooldownRemaining(TEXT("Fire.Ignite"))>0);
    ProfilePawn->SetPlayerState(PlayerState);ProfilePawn->BindPersistentAbilities();
    PlayerState->CommitCooldown(TEXT("Fire.Ignite"),Fire->CooldownGroup,7,3,ProfilePawn->CombatTime());
    TestEqual(TEXT("PlayerState uses the same maximum deadline algorithm"),ProfilePawn->SkillCooldownRemaining(TEXT("Fire.Ignite")),7.f);
    PlayerState->CommitCooldown(TEXT("Fire.Ignite"),Fire->CooldownGroup,2,1,ProfilePawn->CombatTime());
    TestEqual(TEXT("Persistent recommit cannot shorten cooldown"),ProfilePawn->SkillCooldownRemaining(TEXT("Fire.Ignite")),7.f);
    auto* Replacement=World->SpawnActor<AAetherFrontierCharacter>();
    if(!TestNotNull(TEXT("Replacement profile pawn"),Replacement))return false;
    Replacement->SetPlayerState(PlayerState);Replacement->BindPersistentAbilities();
    TestEqual(TEXT("Profile avatar replacement retains persistent owner's cooldown"),Replacement->SkillCooldownRemaining(TEXT("Fire.Ignite")),7.f);
    TestTrue(TEXT("Replaced profile avatar fails closed"),ProfilePawn->SkillCooldownRemaining(TEXT("Fire.Ignite"))>0);
    Fresh->SetPlayerState(PlayerState);
    TestTrue(TEXT("Definition actor with a profile cannot borrow either cooldown authority"),Fresh->SkillCooldownRemaining(TEXT("Fire.Ignite"))>0);
    return true;
}
#endif
