#include "Misc/AutomationTest.h"
#include "Characters/AetherFrontierCharacter.h"
#include "Framework/AetherFrontierMode.h"
#include "Definitions/AetherV10Definitions.h"
#include "Skills/AetherNpcSkillDefinitions.h"
#include "Skills/AetherSkillAbilityBinding.h"
#include "Engine/World.h"
#include "World/AetherEncounters.h"
#include "EngineUtils.h"
#include "Framework/AetherAdventure.h"
#include "Misc/ScopeExit.h"
#include "Kismet/GameplayStatics.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherNpcSkillAuthorityTest,"Aether.Systems.Skills.ProfileAndDefinitionAuthority",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherNpcSkillAuthorityTest::RunTest(const FString&)
{
    auto* W=UWorld::CreateWorld(EWorldType::Game,false);if(!TestNotNull(TEXT("Isolated authority world"),W))return false;
    ON_SCOPE_EXIT {W->EndPlay(EEndPlayReason::Quit);W->DestroyWorld(false);};
    auto* Mode=W->SpawnActor<AAetherFrontierMode>();auto* Player=W->SpawnActor<AAetherFrontierCharacter>();
    if(!Mode||!Player)return false;
    Player->AbilitySystem->AddAttributeSetSubobject(Player->Attributes.Get());Player->AbilitySystem->InitAbilityActorInfo(Player,Player);
    Player->GrantSpells();
    TestTrue(TEXT("Player spawn chooses persistent profile authority before possession"),Player->SkillAuthority==EAetherSkillAuthority::Profile);
    TestFalse(TEXT("No profile snapshot cannot fall back to definition skills"),Player->SkillUnlocked(TEXT("Fire.Ignite")));
    TestNull(TEXT("Player loading never gets a temporary NPC fire spec"),AetherSkillBinding::Find(*Player->AbilitySystem,TEXT("Fire.Ignite")));
    FString SupportLoadout;
    for(const auto& Target:FAetherV10Definitions::Get().Interactions.Targets)for(const auto& A:Target.Value.Actions)
        if(A.Kind==EAetherInteractionActionKind::RecruitHealer)SupportLoadout=A.ServiceId;
    if(!TestFalse(TEXT("Recruitment supplies a concrete data-defined capability"),SupportLoadout.IsEmpty()))return false;
    Player->SkillLoadoutId=SupportLoadout;FString Why;
    TestFalse(TEXT("NPC configuration cannot authorize a persistent player"),Player->GrantDefinitionSkills(Why));
    auto* Healer=Mode->SpawnFighter(FVector(200,0,0),EAetherFighter::Player,NAME_None,SupportLoadout);
    if(!TestNotNull(TEXT("Production spawn resolves healer definition"),Healer))return false;
    Healer->AbilitySystem->AddAttributeSetSubobject(Healer->Attributes.Get());Healer->AbilitySystem->InitAbilityActorInfo(Healer,Healer);
    Healer->GrantSpells();
    TestTrue(TEXT("Production NPC spawn chooses definition authority"),Healer->SkillAuthority==EAetherSkillAuthority::Definition);
    TestNotNull(TEXT("Real GAS receives support Water.Draw"),AetherSkillBinding::Find(*Healer->AbilitySystem,TEXT("Water.Draw")));
    TestTrue(TEXT("NPC support can execute its granted current skill"),Healer->SkillUnlocked(TEXT("Water.Draw")));
    TestFalse(TEXT("Support definition does not grant unrelated fire"),Healer->SkillUnlocked(TEXT("Fire.Ignite")));
    AddExpectedError(TEXT("AETHER_NPC_SPAWN_REJECTED"),EAutomationExpectedErrorFlags::Contains,1);
    TestNull(TEXT("Missing NPC definition rejects production spawn"),Mode->SpawnFighter(FVector(400,0,0),EAetherFighter::FireCaster,NAME_None,TEXT("Missing.Definition")));
    auto* Director=W->SpawnActor<AAetherEncounterDirector>();
    FAetherCamp Camp;Camp.Definition=TEXT("SyntheticFailureOnly");
    FAetherEncounterRule Rule;Rule.Types={uint8(EAetherFighter::ShieldGuard),255};
    const auto LiveFighters=[&]{int32 Count=0;for(TActorIterator<AAetherFrontierCharacter> It(W);It;++It)if(!It->IsActorBeingDestroyed())++Count;return Count;};
    const int32 Before=LiveFighters();
    AddExpectedError(TEXT("AETHER_NPC_SPAWN_REJECTED"),EAutomationExpectedErrorFlags::Contains,1);
    AddExpectedError(TEXT("AETHER_CAMP_SPAWN_FAILED"),EAutomationExpectedErrorFlags::Contains,1);
    TestFalse(TEXT("Production camp assembly fails on unsupported second NPC"),Director->SpawnCamp(*Mode,Camp,Rule));
    TestTrue(TEXT("Failure has explicit unavailable state and removes partial NPCs"),Camp.bSpawnFailed&&!Camp.bSpawned&&Camp.Enemies.IsEmpty()&&LiveFighters()==Before);
    TestFalse(TEXT("Unavailable NPC batch is never eligible for cleared-camp reward"),Camp.CanCreateClearReward());
    TestFalse(TEXT("Unavailable batch has no encounter instance to settle"),Camp.Instance.IsValid());
    TestTrue(TEXT("Failed NPC assembly created no kill/quest credit"),Mode->KillCredit.IsEmpty());
    auto* Adventure=W->SpawnActor<AAetherAdventureMode>();
    auto* Probe=Cast<AAetherCharacter>(Adventure->SpawnDefaultPawnAtTransform_Implementation(nullptr,FTransform(FVector(600,0,0))));
    if(!TestNotNull(TEXT("Supported Adventure probe explicitly configures data capabilities"),Probe))return false;
    Probe->AbilitySystem->AddAttributeSetSubobject(Probe->Attributes.Get());Probe->AbilitySystem->InitAbilityActorInfo(Probe,Probe);Probe->GrantSpells();
    TestTrue(TEXT("Adventure probe retained canonical frost access"),Probe->SkillUnlocked(TEXT("Frost.Freeze")));
    Adventure->SaveSlot=TEXT("Aether_NpcFailure_")+FGuid::NewGuid().ToString(EGuidFormats::Digits);
    Adventure->DefaultPawnClass=nullptr;
    AddExpectedError(TEXT("AETHER_ADVENTURE_ASSEMBLY_FAILED"),EAutomationExpectedErrorFlags::Contains,1);
    TestNull(TEXT("Unavailable probe actor class marks assembly failed"),Adventure->SpawnDefaultPawnAtTransform_Implementation(nullptr,FTransform::Identity));
    TestTrue(TEXT("Failed assembly blocks direct progression entry"),Adventure->Interact(Probe,false).StartsWith(TEXT("AETHER_ADVENTURE_UNAVAILABLE:")));
    TestTrue(TEXT("Failed assembly blocks direct save entry before writing"),Adventure->SaveAdventure(Probe).StartsWith(TEXT("AETHER_ADVENTURE_UNAVAILABLE:")));
    TestTrue(TEXT("Failed assembly blocks direct load entry before restoration"),Adventure->LoadAdventure(Probe).StartsWith(TEXT("AETHER_ADVENTURE_UNAVAILABLE:")));
    TestFalse(TEXT("Rejected save did not create the isolated fixture slot"),UGameplayStatics::DoesSaveGameExist(Adventure->SaveSlot,0));
    Healer->SkillLoadoutId=TEXT("Missing.Definition");
    TestFalse(TEXT("Lost definition cannot keep exercising an old grant"),Healer->SkillUnlocked(TEXT("Water.Draw")));
    return true;
}
#endif
