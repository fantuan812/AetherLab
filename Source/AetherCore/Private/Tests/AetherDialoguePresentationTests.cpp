#include "Misc/AutomationTest.h"
#include "Definitions/AetherV10Definitions.h"
#include "Interaction/AetherDialoguePresentation.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherDialogueDefinitionsTest,"Aether.Systems.Dialogue.CurrentPresentationDefinitions",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherDialogueDefinitionsTest::RunTest(const FString&)
{
    const auto& D=FAetherV10Definitions::Get();if(!TestTrue(*D.Error,D.bValid))return false;
    FString Json;if(!TestTrue(TEXT("Current interaction source exists"),FFileHelper::LoadFileToString(Json,*(FPaths::ProjectContentDir()/TEXT("AetherCore/Definitions/V10/Interactions.json")))))return false;
    const auto Change=[&](TFunctionRef<void(FJsonObject&)> Edit)
    {
        TSharedPtr<FJsonObject> Root;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Root);Edit(*Root);
        FString Text,Why;FJsonSerializer::Serialize(Root.ToSharedRef(),TJsonWriterFactory<>::Create(&Text));
        const auto Parsed=FAetherInteractionDefinitions::Parse(Text,D.Rules,D.Economy,Why);return Parsed.Validate(D.Rules,D.Economy,Why);
    };
    TestFalse(TEXT("Old schema is not converted"),Change([](auto& R){R.SetNumberField(TEXT("SchemaVersion"),1);}));
    TestFalse(TEXT("Missing camera policy is not assumed"),Change([](auto& R){R.GetArrayField(TEXT("Presentations"))[0]->AsObject()->RemoveField(TEXT("Camera"));}));
    TestFalse(TEXT("Unknown profile field rejected"),Change([](auto& R){R.GetArrayField(TEXT("Presentations"))[0]->AsObject()->SetBoolField(TEXT("Guess"),true);}));
    TestFalse(TEXT("Wrong current line field casing rejected"),Change([](auto& R){auto L=R.GetArrayField(TEXT("Targets"))[1]->AsObject()->GetArrayField(TEXT("Dialogue"))[0]->AsObject()->GetArrayField(TEXT("Lines"))[0]->AsObject();L->RemoveField(TEXT("DurationSeconds"));L->SetNumberField(TEXT("durationSeconds"),2);}));
    TestFalse(TEXT("Legacy paragraph field is not accepted alongside lines"),Change([](auto& R){R.GetArrayField(TEXT("Targets"))[1]->AsObject()->GetArrayField(TEXT("Dialogue"))[0]->AsObject()->SetStringField(TEXT("Text"),TEXT("Old paragraph"));}));
    TestFalse(TEXT("Unknown presentation rejected"),Change([](auto& R){R.GetArrayField(TEXT("Targets"))[1]->AsObject()->GetArrayField(TEXT("Dialogue"))[0]->AsObject()->SetStringField(TEXT("PresentationId"),TEXT("Missing"));}));
    TestFalse(TEXT("Presentation reference casing is exact"),Change([](auto& R){R.GetArrayField(TEXT("Targets"))[1]->AsObject()->GetArrayField(TEXT("Dialogue"))[0]->AsObject()->SetStringField(TEXT("PresentationId"),TEXT("conversation"));}));
    TestFalse(TEXT("Action entry dialogue reference casing is exact"),Change([](auto& R){R.GetArrayField(TEXT("Targets"))[1]->AsObject()->GetArrayField(TEXT("Actions"))[0]->AsObject()->SetStringField(TEXT("DialogueId"),TEXT("greeting"));}));
    TestFalse(TEXT("Unknown action field cannot hide authored behavior"),Change([](auto& R){R.GetArrayField(TEXT("Targets"))[1]->AsObject()->GetArrayField(TEXT("Actions"))[0]->AsObject()->SetNumberField(TEXT("WaitSeconds"),4);}));
    TestFalse(TEXT("Negative subtitle duration rejected"),Change([](auto& R){R.GetArrayField(TEXT("Targets"))[1]->AsObject()->GetArrayField(TEXT("Dialogue"))[0]->AsObject()->GetArrayField(TEXT("Lines"))[0]->AsObject()->SetNumberField(TEXT("DurationSeconds"),-1);}));
    TestFalse(TEXT("Camera FOV bounds enforced"),Change([](auto& R){R.GetArrayField(TEXT("Presentations"))[0]->AsObject()->GetObjectField(TEXT("Camera"))->SetNumberField(TEXT("Fov"),180);}));
    TestFalse(TEXT("Services cannot require an unskippable fixed wait"),Change([](auto& R){auto P=R.GetArrayField(TEXT("Presentations"))[0]->AsObject();P->SetBoolField(TEXT("AllowAdvance"),false);P->SetBoolField(TEXT("AllowSkip"),false);}));
    for(const TCHAR* Id:{TEXT("Daily"),TEXT("DailyPatrol"),TEXT("DailyFire")})
    {
        const auto& N=D.Interactions.Targets.FindChecked(Id).Dialogue.FindChecked(TEXT("Greeting"));
        TestFalse(TEXT("Authored board presentation creates no camera"),D.Interactions.Presentations.FindChecked(N.PresentationId).Camera.IsSet());
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherDialogueClockTest,"Aether.Systems.Dialogue.DeterministicSubtitleClock",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherDialogueClockTest::RunTest(const FString&)
{
    FAetherDialoguePlayback P;TArray<FAetherDialogueLine> Lines={{TEXT("First"),.5},{TEXT("Second"),1.5}};
    TestTrue(TEXT("Starts at authored first line"),P.Start(Lines,true,true)&&P.LineIndex()==0);
    TestFalse(TEXT("Does not advance before authored duration"),P.Tick(.25));
    TestTrue(TEXT("Exact boundary advances"),P.Tick(.25)&&P.LineIndex()==1);
    TestTrue(TEXT("Bounded long frame completes all remaining lines"),P.Tick(1000)&&P.Phase()==EAetherDialoguePlaybackPhase::Choices);
    TestFalse(TEXT("Finished playback cannot advance again"),P.Advance());
    Lines[0].DurationSeconds=2;P.Start(Lines,true,true);
    TestFalse(TEXT("Changed authored duration is consumed without compiled timing"),P.Tick(.5));
    TestTrue(TEXT("Manual advance ignores remaining wait"),P.Advance()&&P.LineIndex()==1);
    TestTrue(TEXT("Skip opens choices directly"),P.Skip()&&P.Phase()==EAetherDialoguePlaybackPhase::Choices);
    P.Start(Lines,false,true);TestFalse(TEXT("Disabled advance is enforced"),P.Advance());TestTrue(TEXT("Allowed skip still works"),P.Skip());
    P.Start(Lines,true,false);TestFalse(TEXT("Disabled skip is enforced"),P.Skip());
    P.Reset();TestTrue(TEXT("Close clears clock and selection"),P.Phase()==EAetherDialoguePlaybackPhase::Closed&&P.LineIndex()==INDEX_NONE);
    Lines[0].DurationSeconds=0;TestFalse(TEXT("Invalid fixture timing fails closed"),P.Start(Lines,true,true));
    return true;
}
#endif
