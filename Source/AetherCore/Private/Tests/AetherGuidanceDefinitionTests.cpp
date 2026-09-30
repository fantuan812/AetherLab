#include "Misc/AutomationTest.h"
#include "Definitions/AetherV10Definitions.h"
#include "Definitions/AetherWorldDefinition.h"
#include "Skills/AetherNpcSkillDefinitions.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherGuidanceDefinitionTest,"Aether.Systems.Guidance.CanonicalDefinitions",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherGuidanceDefinitionTest::RunTest(const FString&)
{
    FString Json;if(!TestTrue(TEXT("Staged guidance definition exists"),FFileHelper::LoadFileToString(Json,*(FPaths::ProjectContentDir()/TEXT("AetherCore/Definitions/V10/Guidance.json")))))return false;
    const auto& D=FAetherV10Definitions::Get();if(!TestTrue(*D.Error,D.bValid))return false;
    auto Parse=[&](const FString& Text){return FAetherGuidanceDefinitions::Parse(Text,D.Rules,FAetherWorldDefinitions::Get(),D.Skills,FAetherNpcSkillDefinitions::Get());};
    TestTrue(TEXT("All references resolve against current catalogs"),Parse(Json).bValid);
    auto Change=[&](TFunctionRef<void(FJsonObject&)> Edit)
    {
        TSharedPtr<FJsonObject> Root;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Root);Edit(*Root);
        FString Text;FJsonSerializer::Serialize(Root.ToSharedRef(),TJsonWriterFactory<>::Create(&Text));return Parse(Text);
    };
    TestFalse(TEXT("Missing schema has no fallback"),Parse(TEXT("{}")).bValid);
    TestFalse(TEXT("Unknown version rejected"),Change([](auto& R){R.SetNumberField(TEXT("SchemaVersion"),2);}).bValid);
    TestFalse(TEXT("Wrong field spelling rejected"),Change([](auto& R){auto M=R.GetObjectField(TEXT("Messages"));R.RemoveField(TEXT("Messages"));R.SetObjectField(TEXT("messages"),M);}).bValid);
    TestFalse(TEXT("Unknown objective rejected"),Change([](auto& R){R.GetArrayField(TEXT("DynamicObjectives"))[0]->AsObject()->SetStringField(TEXT("ObjectiveId"),TEXT("Unknown"));}).bValid);
    TestFalse(TEXT("Duplicate selector rejected"),Change([](auto& R){auto A=R.GetArrayField(TEXT("DynamicObjectives"));A.Add(A[0]);R.SetArrayField(TEXT("DynamicObjectives"),A);}).bValid);
    TestFalse(TEXT("Unknown selector kind rejected"),Change([](auto& R){R.GetArrayField(TEXT("DynamicObjectives"))[0]->AsObject()->SetStringField(TEXT("Kind"),TEXT("AnyActor"));}).bValid);
    TestFalse(TEXT("An objective without a service/spawner cannot masquerade as an owned service"),Change([](auto& R){R.GetArrayField(TEXT("DynamicObjectives"))[0]->AsObject()->SetStringField(TEXT("SelectorId"),TEXT("Melee1"));}).bValid);
    TestFalse(TEXT("Unknown owned service rejected"),Change([](auto& R){R.GetArrayField(TEXT("DynamicObjectives"))[0]->AsObject()->SetStringField(TEXT("SelectorId"),TEXT("MissingService"));}).bValid);
    TestFalse(TEXT("Wrong service spelling rejected"),Change([](auto& R){R.GetArrayField(TEXT("DynamicObjectives"))[0]->AsObject()->SetStringField(TEXT("SelectorId"),TEXT("trainingextinguished"));}).bValid);
    TestFalse(TEXT("Unknown preparation quest rejected"),Change([](auto& R){R.GetArrayField(TEXT("Preparations"))[0]->AsObject()->SetStringField(TEXT("QuestId"),TEXT("Unknown"));}).bValid);
    TestFalse(TEXT("Unknown phase rejected"),Change([](auto& R){R.GetArrayField(TEXT("EncounterPhases"))[0]->AsObject()->SetStringField(TEXT("Phase"),TEXT("Guess"));}).bValid);
    TestFalse(TEXT("Ambiguous anchor and center rejected"),Change([](auto& R){R.GetArrayField(TEXT("EncounterPhases"))[0]->AsObject()->SetStringField(TEXT("AnchorId"),TEXT("Teacher"));}).bValid);
    TestFalse(TEXT("No copied coordinate field allowed"),Change([](auto& R){R.GetObjectField(TEXT("Completion"))->SetNumberField(TEXT("PositionX"),1);}).bValid);
    FString RulesJson;if(!TestTrue(TEXT("Current training definition exists"),FFileHelper::LoadFileToString(RulesJson,*(FPaths::ProjectContentDir()/TEXT("AetherCore/Definitions/Rules.json")))))return false;
    auto TrainingChange=[&](TFunctionRef<void(FJsonObject&)> Edit)
    {
        TSharedPtr<FJsonObject> Root;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(RulesJson),Root);Edit(*Root);
        FString Text;FJsonSerializer::Serialize(Root.ToSharedRef(),TJsonWriterFactory<>::Create(&Text));return FAetherRules::Parse(Text);
    };
    TestFalse(TEXT("Missing training policy has no fallback"),TrainingChange([](auto& R){R.RemoveField(TEXT("PersonalTraining"));}).bValid);
    TestFalse(TEXT("Training gate must be its declared prerequisite"),TrainingChange([](auto& R){R.GetObjectField(TEXT("PersonalTraining"))->SetStringField(TEXT("RequiredQuestId"),TEXT("Q_Main_08"));}).bValid);
    TestFalse(TEXT("Training fire must belong to its quest"),TrainingChange([](auto& R){R.GetObjectField(TEXT("PersonalTraining"))->SetStringField(TEXT("FireObjectiveId"),TEXT("ForestFire0"));}).bValid);
    TestFalse(TEXT("Training identity casing is exact"),TrainingChange([](auto& R){R.GetObjectField(TEXT("PersonalTraining"))->SetStringField(TEXT("FireObjectiveId"),TEXT("trainingextinguished"));}).bValid);
    TestFalse(TEXT("Training field spelling is exact"),TrainingChange([](auto& R){auto T=R.GetObjectField(TEXT("PersonalTraining"));T->RemoveField(TEXT("QuestId"));T->SetStringField(TEXT("questId"),TEXT("Q_Main_03"));}).bValid);
    // The spawner capability is authored, not a hidden literal allowlist. Moving that capability removes the old selector.
    const auto Changed=TrainingChange([](auto& R){R.GetObjectField(TEXT("PersonalTraining"))->SetStringField(TEXT("FireObjectiveId"),TEXT("Melee1"));});
    TestTrue(TEXT("A declared personal quest objective can explicitly acquire the training-spawner role"),Changed.bValid);
    TestFalse(TEXT("Guidance must track the actual authored spawner identity"),FAetherGuidanceDefinitions::Parse(Json,Changed,FAetherWorldDefinitions::Get(),D.Skills,FAetherNpcSkillDefinitions::Get()).bValid);
    return true;
}
#endif
