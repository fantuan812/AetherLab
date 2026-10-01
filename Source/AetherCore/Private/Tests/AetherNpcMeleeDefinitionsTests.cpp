#include "Misc/AutomationTest.h"
#include "AI/AetherNpcMeleeDefinitions.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherNpcMeleeCatalogTest,"Aether.AI.Melee.StrictContentAndHealthPartition",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherNpcMeleeCatalogTest::RunTest(const FString&)
{
    FString Json;if(!TestTrue(TEXT("Read authored melee data"),FFileHelper::LoadFileToString(Json,*(FPaths::ProjectContentDir()/TEXT("AetherCore/Definitions/V10/NpcMelee.json")))))return false;
    const auto D=FAetherNpcMeleeDefinitions::Parse(Json);
    if(!TestTrue(*D.Error,D.bValid))return false;
    for(const auto* Fighter:{TEXT("ShieldGuard"),TEXT("FireCaster"),TEXT("BellKnight"),TEXT("Wolf"),TEXT("Golem")})
        TestNotNull(TEXT("All enemy policies are explicitly authored"),D.ForFighter(Fighter));
    TestNull(TEXT("Player has no implicit NPC melee policy"),D.ForFighter(TEXT("Player")));
    TestNull(TEXT("Missing fighter fails closed"),D.ForFighter(TEXT("Missing")));
    TestNull(TEXT("Case aliases are not fighter identities"),D.ForFighter(TEXT("wolf")));
    const auto* Knight=D.ForFighter(TEXT("BellKnight"));const auto* Wolf=D.ForFighter(TEXT("Wolf"));
    if(!Knight||!Wolf)return false;double Delay=0;
    TestTrue(TEXT("Health partition includes zero without fallback"),Knight->TelegraphFor(0,Delay)&&Delay==.6);
    TestTrue(TEXT("Health threshold lower interval excludes its upper boundary"),Knight->TelegraphFor(.499,Delay)&&Delay==.6);
    TestTrue(TEXT("Boundary belongs exactly to the next interval"),Knight->TelegraphFor(.5,Delay)&&Delay==.95);
    TestTrue(TEXT("Last explicit interval includes full health"),Knight->TelegraphFor(1,Delay)&&Delay==.95);
    TestFalse(TEXT("Out-of-domain health is not silently normalized by catalog"),Knight->TelegraphFor(1.01,Delay));
    TestTrue(TEXT("Wolf declares motion content but no duplicate attack duration"),Wolf->Motion==EAetherNpcAttackMotion::ForwardDuringActive&&Wolf->MotionSpeedCmPerSecond==450);
    const auto Mutate=[&](TFunctionRef<void(FJsonObject&)> Edit)
    {
        TSharedPtr<FJsonObject> R;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),R);Edit(*R);
        FString Text;FJsonSerializer::Serialize(R.ToSharedRef(),TJsonWriterFactory<>::Create(&Text));return FAetherNpcMeleeDefinitions::Parse(Text);
    };
    TestFalse(TEXT("Schema string cannot coerce to Number"),Mutate([](auto& R){R.SetStringField(TEXT("SchemaVersion"),TEXT("1"));}).bValid);
    TestFalse(TEXT("Unsupported schema rejected"),Mutate([](auto& R){R.SetNumberField(TEXT("SchemaVersion"),2);}).bValid);
    for(const auto* Field:{TEXT("AttackId"),TEXT("GuardWhileApproaching"),TEXT("StartRangeMarginCm"),TEXT("HealthBands"),TEXT("Motion")})
        TestFalse(TEXT("Every policy field is required"),Mutate([&](auto& R){R.GetArrayField(TEXT("Profiles"))[0]->AsObject()->RemoveField(Field);}).bValid);
    TestFalse(TEXT("Attack identity has no case alias"),Mutate([](auto& R){R.GetArrayField(TEXT("Profiles"))[0]->AsObject()->SetStringField(TEXT("AttackId"),TEXT("light"));}).bValid);
    TestFalse(TEXT("Unsupported attack executor rejected"),Mutate([](auto& R){R.GetArrayField(TEXT("Profiles"))[0]->AsObject()->SetStringField(TEXT("AttackId"),TEXT("Special"));}).bValid);
    TestFalse(TEXT("Range string cannot coerce to Number"),Mutate([](auto& R){R.GetArrayField(TEXT("Profiles"))[0]->AsObject()->SetStringField(TEXT("StartRangeMarginCm"),TEXT("10"));}).bValid);
    TestFalse(TEXT("Guard policy requires Boolean"),Mutate([](auto& R){R.GetArrayField(TEXT("Profiles"))[0]->AsObject()->SetNumberField(TEXT("GuardWhileApproaching"),1);}).bValid);
    TestFalse(TEXT("Health partition must explicitly cover its tail"),Mutate([](auto& R){R.GetArrayField(TEXT("Profiles"))[0]->AsObject()->GetArrayField(TEXT("HealthBands"))[0]->AsObject()->SetNumberField(TEXT("MaxHealthFraction"),.9);}).bValid);
    TestFalse(TEXT("Repeated health boundary cannot create an overlap"),Mutate([](auto& R){auto P=R.GetArrayField(TEXT("Profiles"))[2]->AsObject();auto B=P->GetArrayField(TEXT("HealthBands"));B.Insert(B[0],0);P->SetArrayField(TEXT("HealthBands"),B);}).bValid);
    TestFalse(TEXT("Zero boundary cannot create an empty interval"),Mutate([](auto& R){R.GetArrayField(TEXT("Profiles"))[2]->AsObject()->GetArrayField(TEXT("HealthBands"))[0]->AsObject()->SetNumberField(TEXT("MaxHealthFraction"),0);}).bValid);
    TestFalse(TEXT("Unknown motion policy rejected"),Mutate([](auto& R){R.GetArrayField(TEXT("Profiles"))[3]->AsObject()->GetObjectField(TEXT("Motion"))->SetStringField(TEXT("Policy"),TEXT("Launch"));}).bValid);
    TestFalse(TEXT("None cannot hide unconsumed motion speed"),Mutate([](auto& R){R.GetArrayField(TEXT("Profiles"))[0]->AsObject()->GetObjectField(TEXT("Motion"))->SetNumberField(TEXT("SpeedCmPerSecond"),450);}).bValid);
    TestFalse(TEXT("Moving policy must have positive explicit speed"),Mutate([](auto& R){R.GetArrayField(TEXT("Profiles"))[3]->AsObject()->GetObjectField(TEXT("Motion"))->SetNumberField(TEXT("SpeedCmPerSecond"),0);}).bValid);
    TestFalse(TEXT("Catalog cannot introduce another recovery authority"),Mutate([](auto& R){R.GetArrayField(TEXT("Profiles"))[0]->AsObject()->SetNumberField(TEXT("RecoverySeconds"),1);}).bValid);
    TestFalse(TEXT("Unknown binding rejected"),Mutate([](auto& R){R.GetObjectField(TEXT("FighterProfiles"))->SetStringField(TEXT("Wolf"),TEXT("Missing"));}).bValid);
    return true;
}
#endif
