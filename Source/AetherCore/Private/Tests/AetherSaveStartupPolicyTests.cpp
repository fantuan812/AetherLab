#include "Misc/AutomationTest.h"
#include "Persistence/AetherSaveStartupPolicy.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherSaveStartupPolicyTest,"Aether.Systems.Persistence.CurrentSchemaStartup",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherSaveStartupPolicyTest::RunTest(const FString&)
{
    FString Why;
    TestTrue(TEXT("Valid native world keeps priority over archived old files"),AetherSaveStartup::CanOpen(true,true,true,false,Why)&&Why.IsEmpty());
    TestFalse(TEXT("Historical file cannot silently become empty progress"),AetherSaveStartup::CanOpen(false,false,true,true,Why));
    TestTrue(TEXT("Unsupported historical format has a stable diagnostic"),Why.StartsWith(TEXT("AETHER_SAVE_FORMAT_UNSUPPORTED:")));
    TestFalse(TEXT("Orphan native records cannot be replaced by new game"),AetherSaveStartup::CanOpen(false,true,false,true,Why));
    TestTrue(TEXT("Missing world is reported separately"),Why.StartsWith(TEXT("AETHER_SAVE_WORLD_MISSING:")));
    TestFalse(TEXT("Missing data is not implicit new-game authorization"),AetherSaveStartup::CanOpen(false,false,false,false,Why));
    TestTrue(TEXT("Empty authorized namespace can initialize current schema"),AetherSaveStartup::CanOpen(false,false,false,true,Why)&&Why.IsEmpty());
    TestTrue(TEXT("Explicit isolated prefix allowed"),AetherSaveStartup::ValidPrefix(TEXT("Native_Acceptance-001")));
    for(const FString Prefix:{FString(),FString(TEXT("../Profile")),FString(TEXT("folder/name")),FString(TEXT("folder\\name")),FString::ChrN(65,'x')})
        TestFalse(TEXT("Prefix cannot escape its namespace"),AetherSaveStartup::ValidPrefix(Prefix));
    FString Prefix=TEXT("DefaultProfile");
    TestFalse(TEXT("Invalid explicit namespace does not fall back to default"),AetherSaveStartup::SelectPrefix(true,TEXT("../Other"),Prefix,Why));
    TestEqual(TEXT("Invalid request never mutates target"),Prefix,FString(TEXT("DefaultProfile")));
    TestFalse(TEXT("Empty explicit namespace rejected"),AetherSaveStartup::SelectPrefix(true,TEXT(""),Prefix,Why));
    TestTrue(TEXT("Exactly 64 valid characters agree with policy"),AetherSaveStartup::SelectPrefix(true,FString::ChrN(64,'x'),Prefix,Why)&&Prefix.Len()==64);
    return true;
}
#endif
