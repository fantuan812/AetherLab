#include "Misc/AutomationTest.h"
#include "Combat/AetherControlledActionDefinition.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherControlledCatalogTest,"Aether.Systems.Actions.CanonicalCatalog",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherControlledCatalogTest::RunTest(const FString&)
{
    FString Json;
    if(!TestTrue(TEXT("Staged current action file exists"),FFileHelper::LoadFileToString(Json,*(FPaths::ProjectContentDir()/TEXT("AetherCore/Definitions/V10/Actions.json")))))return false;
    const auto Catalog=FAetherControlledActionCatalog::Parse(Json);
    if(!TestTrue(*Catalog.Error,Catalog.bValid))return false;
    TestFalse(TEXT("No empty/default fallback"),FAetherControlledActionCatalog::Parse(TEXT("{}")).bValid);
    auto Mutate=[&](TFunctionRef<void(FJsonObject&)> Edit)
    {
        TSharedPtr<FJsonObject> Root;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Root);Edit(*Root);
        FString Text;FJsonSerializer::Serialize(Root.ToSharedRef(),TJsonWriterFactory<>::Create(&Text));return FAetherControlledActionCatalog::Parse(Text);
    };
    TestFalse(TEXT("Unknown schema is rejected"),Mutate([](auto& R){R.SetNumberField(TEXT("SchemaVersion"),2);}).bValid);
    TestFalse(TEXT("Field spelling is case-sensitive for runtime and authoring"),Mutate([](auto& R){const auto Input=R.GetObjectField(TEXT("Input"));R.RemoveField(TEXT("Input"));R.SetObjectField(TEXT("input"),Input);}).bValid);
    TestFalse(TEXT("Unknown fields are rejected"),Mutate([](auto& R){R.SetBoolField(TEXT("LegacyFallback"),true);}).bValid);
    TestFalse(TEXT("Missing input is rejected"),Mutate([](auto& R){R.RemoveField(TEXT("Input"));}).bValid);
    TestFalse(TEXT("Negative charge is rejected"),Mutate([](auto& R){R.GetObjectField(TEXT("Input"))->SetNumberField(TEXT("AttackCharge"),-1);}).bValid);
    TestFalse(TEXT("Longer buffer than charge is rejected"),Mutate([](auto& R){R.GetObjectField(TEXT("Input"))->SetNumberField(TEXT("AttackBuffer"),11);}).bValid);
    TestFalse(TEXT("Partial negative commit is not no-commit sentinel"),Mutate([](auto& R){R.GetArrayField(TEXT("Actions"))[0]->AsObject()->SetNumberField(TEXT("CommitTime"),-.5);}).bValid);
    TestFalse(TEXT("Duplicate identities rejected"),Mutate([](auto& R){auto Rows=R.GetArrayField(TEXT("Actions"));Rows.Add(Rows[0]);R.SetArrayField(TEXT("Actions"),Rows);}).bValid);
    TestFalse(TEXT("Missing capability rejected"),Mutate([](auto& R){auto Rows=R.GetArrayField(TEXT("Actions"));Rows.RemoveAt(0);R.SetArrayField(TEXT("Actions"),Rows);}).bValid);
    TestFalse(TEXT("Unknown contact policy rejected"),Mutate([](auto& R){R.GetArrayField(TEXT("Actions"))[0]->AsObject()->SetStringField(TEXT("ContactPolicy"),TEXT("Guess"));}).bValid);
    TestFalse(TEXT("Vault phase sum must match duration"),Mutate([](auto& R){for(const auto& V:R.GetArrayField(TEXT("Actions")))if(V->AsObject()->GetStringField(TEXT("ActionId"))==TEXT("Vault"))V->AsObject()->SetNumberField(TEXT("Duration"),2);}).bValid);
    TestFalse(TEXT("Wrong identity case has no alias"),Mutate([](auto& R){R.GetArrayField(TEXT("Actions"))[0]->AsObject()->SetStringField(TEXT("ActionId"),TEXT("crouchidle"));}).bValid);
    TestFalse(TEXT("Presentation cost is unsupported rather than ignored"),Mutate([](auto& R){R.GetArrayField(TEXT("Actions"))[0]->AsObject()->SetNumberField(TEXT("Cost"),2);}).bValid);
    TestFalse(TEXT("Ability loops are unsupported"),Mutate([](auto& R){for(const auto& V:R.GetArrayField(TEXT("Actions")))if(V->AsObject()->GetStringField(TEXT("ActionId"))==TEXT("Vault"))V->AsObject()->SetBoolField(TEXT("Loop"),true);}).bValid);
    TestFalse(TEXT("Unsupported directional stance rejected"),Mutate([](auto& R){for(const auto& V:R.GetArrayField(TEXT("Actions")))if(V->AsObject()->GetStringField(TEXT("ActionId"))==TEXT("DodgeLeft"))V->AsObject()->SetNumberField(TEXT("AllowedStances"),2);}).bValid);
    const auto Changed=Mutate([](auto& R){R.GetObjectField(TEXT("Input"))->SetNumberField(TEXT("AttackCharge"),.6);});
    TestTrue(TEXT("Valid authored values are consumed, not replaced by compiled defaults"),Changed.bValid&&FMath::IsNearlyEqual(Changed.AttackCharge,.6f));
    return true;
}
#endif
