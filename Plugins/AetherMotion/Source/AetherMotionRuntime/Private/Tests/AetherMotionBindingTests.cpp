#include "AetherMotionBinding.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherMotionBindingContract,"Aether.Motion.BindingContract",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherMotionBindingContract::RunTest(const FString&)
{
 FString Text,Why;TArray<FAetherMotionBinding> Rows;
 if(!TestTrue(TEXT("Current binding file exists"),FFileHelper::LoadFileToString(Text,*AetherMotionBindings::DefinitionPath())))return false;
 TestTrue(TEXT("Current contract parses"),AetherMotionBindings::Parse(Text,Rows,Why));
 TestEqual(TEXT("Two available bodies and one draft"),Rows.Num(),3);
 int32 Configured=0;for(const auto& B:Rows){Configured+=B.Configured?1:0;if(B.Id==TEXT("Quaternius65")){TestFalse(TEXT("Native rig is not falsely ready"),B.Configured);TestTrue(TEXT("Draft contributes no runtime dependencies"),B.RuntimeAssets().IsEmpty());}}
 TestEqual(TEXT("Existing Manny/Quinn remain configured"),Configured,2);
 const auto* Manny=AetherMotionBindings::ForMesh(FSoftObjectPath(TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple")),Why);
 TestNotNull(TEXT("Exact existing mesh resolves"),Manny);
 TestNull(TEXT("Similar mesh name never falls back"),AetherMotionBindings::ForMesh(FSoftObjectPath(TEXT("/Game/Other/SKM_Manny_Simple.SKM_Manny_Simple")),Why));
 TestFalse(TEXT("Case-sensitive field spelling"),AetherMotionBindings::Parse(Text.Replace(TEXT("\"bindings\""),TEXT("\"Bindings\"")),Rows,Why));
 TestFalse(TEXT("Unknown schema rejected"),AetherMotionBindings::Parse(Text.Replace(TEXT("\"schema\": 1"),TEXT("\"schema\": 2")),Rows,Why));
 TestFalse(TEXT("Duplicate mesh authority rejected"),AetherMotionBindings::Parse(Text.Replace(TEXT("/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple"),TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple")),Rows,Why));
 TestNotNull(TEXT("UE soft-path casing refers to the same asset"),AetherMotionBindings::ForMesh(FSoftObjectPath(TEXT("/game/characters/mannequins/meshes/skm_manny_simple.skm_manny_simple")),Why));
 TestFalse(TEXT("Source cannot alias a target rig"),AetherMotionBindings::Parse(Text.Replace(TEXT("\"target_rig\": \"/Game/Animation/Motion/IK_Manny\""),TEXT("\"target_rig\": \"/Game/Animation/Motion/IK_G1\"")),Rows,Why));
 TestFalse(TEXT("Source alias rejection uses UE path identity"),AetherMotionBindings::Parse(Text.Replace(TEXT("\"target_rig\": \"/Game/Animation/Motion/IK_Manny\""),TEXT("\"target_rig\": \"/Game/Animation/Motion/ik_g1\"")),Rows,Why));
 TestTrue(TEXT("Failed parse never retains previous bindings"),Rows.IsEmpty());
 return true;
}
#endif
