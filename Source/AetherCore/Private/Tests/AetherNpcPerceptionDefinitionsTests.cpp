#include "Misc/AutomationTest.h"
#include "AI/AetherNpcPerceptionDefinitions.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherNpcPerceptionCatalogTest,"Aether.AI.Perception.StrictCatalog",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherNpcPerceptionCatalogTest::RunTest(const FString&)
{
    FString Json;if(!TestTrue(TEXT("Read authored perception data"),FFileHelper::LoadFileToString(Json,*(FPaths::ProjectContentDir()/TEXT("AetherCore/Definitions/V10/NpcPerception.json")))))return false;
    const auto D=FAetherNpcPerceptionDefinitions::Parse(Json);
    if(!TestTrue(*D.Error,D.bValid))return false;
    const auto* Standard=D.ForFighter(TEXT("ShieldGuard"));
    if(!TestNotNull(TEXT("Explicit enemy binding"),Standard))return false;
    TestEqual(TEXT("Authored polling interval"),Standard->SampleIntervalSeconds,.25);
    TestEqual(TEXT("Authored sight radius"),Standard->SightRadiusCm,1300.);
    for(const auto* Fighter:{TEXT("FireCaster"),TEXT("BellKnight"),TEXT("Wolf"),TEXT("Golem")})
        TestNotNull(TEXT("All existing enemy identities have explicit data"),D.ForFighter(Fighter));
    TestNull(TEXT("Player/companion behavior is not implicitly controlled"),D.ForFighter(TEXT("Player")));
    TestNull(TEXT("Missing fighter binding has no fallback"),D.ForFighter(TEXT("Missing")));
    TestNull(TEXT("Fighter case aliases are not identities"),D.ForFighter(TEXT("shieldguard")));
    TestNull(TEXT("Profile case aliases are not identities"),D.Find(TEXT("enemy.standard")));
    const auto Mutate=[&](TFunctionRef<void(FJsonObject&)> Edit)
    {
        TSharedPtr<FJsonObject> Root;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Root);Edit(*Root);
        FString Text;FJsonSerializer::Serialize(Root.ToSharedRef(),TJsonWriterFactory<>::Create(&Text));return FAetherNpcPerceptionDefinitions::Parse(Text);
    };
    for(const auto* Field:{TEXT("SampleIntervalSeconds"),TEXT("SightRadiusCm"),TEXT("TargetHomeRadiusCm"),TEXT("SelfLeashRadiusCm"),
        TEXT("MemorySeconds"),TEXT("ObservationFreshnessSeconds"),TEXT("HomeArrivalRadiusCm")})
    {
        TestFalse(TEXT("Each numeric policy field is required"),Mutate([&](auto& R){R.GetArrayField(TEXT("Profiles"))[0]->AsObject()->RemoveField(Field);}).bValid);
        TestFalse(TEXT("No nonpositive policy substitutes defaults"),Mutate([&](auto& R){R.GetArrayField(TEXT("Profiles"))[0]->AsObject()->SetNumberField(Field,0);}).bValid);
        TestFalse(TEXT("Wrong numeric type is rejected"),Mutate([&](auto& R){R.GetArrayField(TEXT("Profiles"))[0]->AsObject()->SetStringField(Field,TEXT("1"));}).bValid);
    }
    TestFalse(TEXT("String schema version cannot be numerically coerced"),Mutate([](auto& R){R.SetStringField(TEXT("SchemaVersion"),TEXT("1"));}).bValid);
    TestFalse(TEXT("Profile identity must be a JSON string"),Mutate([](auto& R){
        R.GetArrayField(TEXT("Profiles"))[0]->AsObject()->SetNumberField(TEXT("Id"),1);
        for(auto& P:R.GetObjectField(TEXT("FighterProfiles"))->Values)P.Value=MakeShared<FJsonValueString>(TEXT("1"));
    }).bValid);
    TestFalse(TEXT("Binding identity must be a JSON string"),Mutate([](auto& R){
        R.GetArrayField(TEXT("Profiles"))[0]->AsObject()->SetStringField(TEXT("Id"),TEXT("1"));
        for(auto& P:R.GetObjectField(TEXT("FighterProfiles"))->Values)P.Value=MakeShared<FJsonValueNumber>(1);
    }).bValid);
    TestFalse(TEXT("Old schema rejected"),Mutate([](auto& R){R.SetNumberField(TEXT("SchemaVersion"),0);}).bValid);
    TestFalse(TEXT("Future schema rejected"),Mutate([](auto& R){R.SetNumberField(TEXT("SchemaVersion"),2);}).bValid);
    TestFalse(TEXT("Unknown root fields rejected"),Mutate([](auto& R){R.SetBoolField(TEXT("UseFallback"),true);}).bValid);
    TestFalse(TEXT("Unknown row fields rejected"),Mutate([](auto& R){R.GetArrayField(TEXT("Profiles"))[0]->AsObject()->SetNumberField(TEXT("Sight"),20);}).bValid);
    TestFalse(TEXT("Duplicate profile identity rejected"),Mutate([](auto& R){auto Rows=R.GetArrayField(TEXT("Profiles"));Rows.Add(Rows[0]);R.SetArrayField(TEXT("Profiles"),Rows);}).bValid);
    TestFalse(TEXT("Binding case aliases rejected"),Mutate([](auto& R){R.GetObjectField(TEXT("FighterProfiles"))->SetStringField(TEXT("shieldguard"),TEXT("Enemy.Standard"));}).bValid);
    TestFalse(TEXT("Missing profile binding rejected"),Mutate([](auto& R){R.GetObjectField(TEXT("FighterProfiles"))->SetStringField(TEXT("ShieldGuard"),TEXT("Missing"));}).bValid);
    TestFalse(TEXT("Binding case typo rejected"),Mutate([](auto& R){R.GetObjectField(TEXT("FighterProfiles"))->SetStringField(TEXT("ShieldGuard"),TEXT("enemy.standard"));}).bValid);
    TestFalse(TEXT("This sample-hold algorithm requires freshness at least the polling interval"),Mutate([](auto& R){R.GetArrayField(TEXT("Profiles"))[0]->AsObject()->SetNumberField(TEXT("ObservationFreshnessSeconds"),.1);}).bValid);
    TestFalse(TEXT("Memory cannot expire before freshness"),Mutate([](auto& R){R.GetArrayField(TEXT("Profiles"))[0]->AsObject()->SetNumberField(TEXT("MemorySeconds"),.2);}).bValid);
    TestFalse(TEXT("Target acquisition cannot extend past own leash"),Mutate([](auto& R){R.GetArrayField(TEXT("Profiles"))[0]->AsObject()->SetNumberField(TEXT("TargetHomeRadiusCm"),3000);}).bValid);
    TestFalse(TEXT("Home arrival must remain inside target acquisition radius"),Mutate([](auto& R){R.GetArrayField(TEXT("Profiles"))[0]->AsObject()->SetNumberField(TEXT("HomeArrivalRadiusCm"),2600);}).bValid);
    auto Other=Mutate([](auto& R){auto P=R.GetArrayField(TEXT("Profiles"))[0]->AsObject();P->SetNumberField(TEXT("SampleIntervalSeconds"),.1);P->SetNumberField(TEXT("SightRadiusCm"),700);P->SetNumberField(TEXT("MemorySeconds"),2);});
    const auto* Adjusted=Other.ForFighter(TEXT("ShieldGuard"));
    TestTrue(TEXT("Valid authoring survives without compiled tuning values"),Other.bValid&&Adjusted&&Adjusted->SampleIntervalSeconds==.1&&Adjusted->SightRadiusCm==700&&Adjusted->MemorySeconds==2);
    return true;
}
#endif
