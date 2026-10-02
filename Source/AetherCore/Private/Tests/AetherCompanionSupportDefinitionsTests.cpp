#include "Misc/AutomationTest.h"
#include "AI/AetherCompanionSupportDefinitions.h"
#include "Skills/AetherNpcSkillDefinitions.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherCompanionSupportDefinitionsTest,"Aether.AI.Companion.SupportPolicyContract",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherCompanionSupportDefinitionsTest::RunTest(const FString&)
{
    const FString Json=TEXT(R"JSON({"SchemaVersion":1,"Profiles":[{"Id":"Healer","SampleIntervalSeconds":0.15,"PatientHealthRatioBelow":0.65,"PatientSearchRadiusCm":600,"SelfHealing":{"Enabled":true,"SkillId":"Body.Mend"},"FriendlyHealing":{"Enabled":true,"SkillId":"Body.Aid"},"Cooling":{"Enabled":true,"SkillId":"Water.Draw","AboveTemperatureC":55,"Priority":"BeforeHealing"}}],"LoadoutProfiles":{"Companion.Healer":"Healer"}})JSON");
    const auto Change=[&](TFunction<void(FJsonObject&,FJsonObject&)> Edit)
    {
        TSharedPtr<FJsonObject> Root;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Root);
        auto Profile=Root->GetArrayField(TEXT("Profiles"))[0]->AsObject();Edit(*Root,*Profile);
        FString Out;FJsonSerializer::Serialize(Root.ToSharedRef(),TJsonWriterFactory<>::Create(&Out));return FAetherCompanionSupportDefinitions::Parse(Out);
    };
    auto D=FAetherCompanionSupportDefinitions::Parse(Json);const auto* P=D.ForLoadout(TEXT("Companion.Healer"));FString Why;
    TestTrue(TEXT("Required explicit profile parses"),D.bValid&&P);
    TestNull(TEXT("Loadout binding is case-sensitive"),D.ForLoadout(TEXT("companion.healer")));
    TestNull(TEXT("Unbound guard does not acquire support behavior"),D.ForLoadout(TEXT("Companion.Guard")));
    const auto& Skills=FAetherSkillDefinitionsV10::Get();const auto& Buffs=FAetherBuffDefinitions::Get();
    TestTrue(TEXT("Actual support skills match implemented target/effect semantics"),P&&P->ValidateSkills(Skills,Buffs,Why));
    if(P)
    {
        auto Bad=*P;Bad.SelfHealing.SkillId=TEXT("Body.Aid");TestFalse(TEXT("Friendly aim cannot be substituted for self aim"),Bad.ValidateSkills(Skills,Buffs,Why));
        Bad=*P;Bad.FriendlyHealing.SkillId=TEXT("Body.Haste");TestFalse(TEXT("Support cannot relabel arbitrary buffs as healing"),Bad.ValidateSkills(Skills,Buffs,Why));
        Bad=*P;Bad.Cooling.SkillId=TEXT("Fire.Ignite");TestFalse(TEXT("Cooling must use supported water delivery"),Bad.ValidateSkills(Skills,Buffs,Why));
    }
    for(const auto* Key:{TEXT("Id"),TEXT("SampleIntervalSeconds"),TEXT("PatientHealthRatioBelow"),TEXT("PatientSearchRadiusCm"),TEXT("SelfHealing"),TEXT("FriendlyHealing"),TEXT("Cooling")})
        TestFalse(TEXT("Missing policy field is not a default"),Change([&](auto&,auto& P){P.RemoveField(Key);}).bValid);
    for(const auto* Key:{TEXT("SampleIntervalSeconds"),TEXT("PatientHealthRatioBelow"),TEXT("PatientSearchRadiusCm")})
    {
        TestFalse(TEXT("Numeric string is not accepted"),Change([&](auto&,auto& P){P.SetStringField(Key,TEXT("0.5"));}).bValid);
        TestFalse(TEXT("Zero interval or threshold is invalid"),Change([&](auto&,auto& P){P.SetNumberField(Key,0);}).bValid);
    }
    TestFalse(TEXT("Ratio cannot exceed one"),Change([](auto&,auto& P){P.SetNumberField(TEXT("PatientHealthRatioBelow"),1.1);}).bValid);
    TestFalse(TEXT("Old schema is rejected"),Change([](auto& R,auto&){R.SetNumberField(TEXT("SchemaVersion"),0);}).bValid);
    TestFalse(TEXT("Schema conversion is rejected"),Change([](auto& R,auto&){R.SetStringField(TEXT("SchemaVersion"),TEXT("1"));}).bValid);
    TestFalse(TEXT("Unknown profile field is rejected"),Change([](auto&,auto& P){P.SetNumberField(TEXT("HealAmount"),20);}).bValid);
    for(const auto* Key:{TEXT("SelfHealing"),TEXT("FriendlyHealing"),TEXT("Cooling")})
    {
        TestFalse(TEXT("Missing enable mode is invalid"),Change([&](auto&,auto& P){P.GetObjectField(Key)->RemoveField(TEXT("Enabled"));}).bValid);
        TestFalse(TEXT("Boolean string is invalid"),Change([&](auto&,auto& P){P.GetObjectField(Key)->SetStringField(TEXT("Enabled"),TEXT("true"));}).bValid);
        TestFalse(TEXT("Enabled option needs stable SkillId"),Change([&](auto&,auto& P){P.GetObjectField(Key)->RemoveField(TEXT("SkillId"));}).bValid);
        TestFalse(TEXT("Disabled option cannot silently keep active parameters"),Change([&](auto&,auto& P){P.GetObjectField(Key)->SetBoolField(TEXT("Enabled"),false);}).bValid);
    }
    auto Disabled=Change([](auto&,auto& P){for(const auto* K:{TEXT("SelfHealing"),TEXT("FriendlyHealing"),TEXT("Cooling")}){auto O=MakeShared<FJsonObject>();O->SetBoolField(TEXT("Enabled"),false);P.SetObjectField(K,O);}});
    const auto* NoSupport=Disabled.ForLoadout(TEXT("Companion.Healer"));
    TestTrue(TEXT("Only explicit disabled objects disable support"),NoSupport&&!NoSupport->SelfHealing.bEnabled&&!NoSupport->FriendlyHealing.bEnabled&&!NoSupport->Cooling.bEnabled);
    TestFalse(TEXT("Cooling priority is mandatory"),Change([](auto&,auto& P){P.GetObjectField(TEXT("Cooling"))->RemoveField(TEXT("Priority"));}).bValid);
    TestFalse(TEXT("Unknown priority is rejected"),Change([](auto&,auto& P){P.GetObjectField(TEXT("Cooling"))->SetStringField(TEXT("Priority"),TEXT("Fallback"));}).bValid);
    TestFalse(TEXT("Temperature must be a number"),Change([](auto&,auto& P){P.GetObjectField(TEXT("Cooling"))->SetStringField(TEXT("AboveTemperatureC"),TEXT("55"));}).bValid);
    TestTrue(TEXT("Authored after-healing ordering is supported"),Change([](auto&,auto& P){P.GetObjectField(TEXT("Cooling"))->SetStringField(TEXT("Priority"),TEXT("AfterHealing"));}).bValid);
    const auto& Actual=FAetherCompanionSupportDefinitions::Get();const auto* ActualHealer=Actual.ForLoadout(TEXT("Companion.Healer"));
    TestTrue(TEXT("Shipping policy and actual loadout are both available"),ActualHealer&&FAetherNpcSkillDefinitions::Get().Find(TEXT("Companion.Healer"))&&ActualHealer->ValidateSkills(Skills,Buffs,Why));
    return true;
}
#endif
