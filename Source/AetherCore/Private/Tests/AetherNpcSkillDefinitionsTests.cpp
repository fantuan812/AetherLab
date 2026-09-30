#include "Misc/AutomationTest.h"
#include "Skills/AetherNpcSkillDefinitions.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherNpcSkillCatalogTest,"Aether.Systems.Skills.NpcCapabilityCatalog",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherNpcSkillCatalogTest::RunTest(const FString&)
{
    FString Json;if(!FFileHelper::LoadFileToString(Json,*(FPaths::ProjectContentDir()/TEXT("AetherCore/Definitions/V10/NpcSkills.json"))))return false;
    const auto& Skills=FAetherSkillDefinitionsV10::Get();const auto D=FAetherNpcSkillDefinitions::Parse(Json,Skills);
    if(!TestTrue(*D.Error,D.bValid))return false;
    TestNotNull(TEXT("Data supports healer capabilities"),D.Find(TEXT("Companion.Healer")));
    TestNull(TEXT("Loadout spelling has no case alias"),D.Find(TEXT("companion.healer")));
    TestNull(TEXT("Missing fighter mapping has no default"),D.ForFighter(TEXT("Unknown")));
    auto Mutate=[&](TFunctionRef<void(FJsonObject&)> Edit)
    {
        TSharedPtr<FJsonObject> Root;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Root);Edit(*Root);
        FString Text;FJsonSerializer::Serialize(Root.ToSharedRef(),TJsonWriterFactory<>::Create(&Text));return FAetherNpcSkillDefinitions::Parse(Text,Skills);
    };
    TestFalse(TEXT("Unknown format rejected"),Mutate([](auto& R){R.SetNumberField(TEXT("SchemaVersion"),2);}).bValid);
    TestFalse(TEXT("Duplicate loadouts rejected"),Mutate([](auto& R){auto Rows=R.GetArrayField(TEXT("Loadouts"));Rows.Add(Rows[0]);R.SetArrayField(TEXT("Loadouts"),Rows);}).bValid);
    TestFalse(TEXT("Unknown actor binding rejected"),Mutate([](auto& R){R.GetObjectField(TEXT("FighterLoadouts"))->SetStringField(TEXT("FireCaster"),TEXT("Missing"));}).bValid);
    TestFalse(TEXT("Missing grants are not treated as defaults"),Mutate([](auto& R){R.GetArrayField(TEXT("Loadouts"))[0]->AsObject()->RemoveField(TEXT("InitialGrants"));}).bValid);
    return true;
}
#endif
