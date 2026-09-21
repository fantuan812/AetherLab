#include "Misc/AutomationTest.h"
#include "Skills/AetherSkillDefinitions.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherSkillDefinitionTest,"Aether.V10.Skills.RankDefinitionsAndPrerequisiteValidation",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherSkillDefinitionTest::RunTest(const FString&)
{
    const auto& D=FAetherSkillDefinitionsV10::Get();FString Reason;
    if(!TestTrue(*Reason,D.Validate(Reason)))return false;
    TestEqual(TEXT("Four active and four passive routes"),D.Skills.Num(),8);
    for(const auto& Pair:D.Skills)if(!Pair.Value.bActive)
    {TestEqual(TEXT("Passive route has three ranks"),Pair.Value.Ranks.Num(),3);TestFalse(TEXT("Passive rank changes real attributes"),Pair.Value.Ranks.Last().PassiveStats.IsEmpty());}
    for(int32 Bit=0;Bit<4;++Bit)
    {
        const auto* S=D.Legacy(Bit);if(!TestNotNull(TEXT("Legacy bit has explicit stable mapping"),S))return false;
        TestEqual(TEXT("Three ranks per route"),S->Ranks.Num(),3);
        const auto& Base=S->Ranks[0];const auto& Advanced=S->Ranks[2];
        TestEqual(TEXT("Mandatory story base is free"),Base.PointCost,0);
        TestTrue(TEXT("Upgrade changes mechanics, not just text"),Advanced.HeatJ!=Base.HeatJ||Advanced.WaterKg!=Base.WaterKg||Advanced.ElectricalJ!=Base.ElectricalJ);
        TestTrue(TEXT("Detail and cast use the same table"),D.Effect(S->SkillId,3)==&Advanced);
        TestNull(TEXT("Unsupported rank does not clamp to another rank"),D.Effect(S->SkillId,4));
    }
    TestNull(TEXT("Negative legacy input is not an unassigned definition"),D.Legacy(-1));
    TestEqual(TEXT("Old fire baseline"),D.Effect(TEXT("Fire.Ignite"),1)->HeatJ,60000.);
    TestEqual(TEXT("Old water baseline"),D.Effect(TEXT("Water.Draw"),1)->WaterKg,.5);
    TestEqual(TEXT("Old frost baseline"),D.Effect(TEXT("Frost.Freeze"),1)->HeatJ,-250000.);
    TestEqual(TEXT("Old lightning baseline"),D.Effect(TEXT("Storm.Strike"),1)->ElectricalJ,6000.);
    auto Bad=D;Bad.Skills[TEXT("Fire.Ignite")].Ranks[1].PointCost=-1;
    TestFalse(TEXT("Negative cost rejected"),Bad.Validate(Reason));
    Bad=D;Bad.Skills[TEXT("Fire.Ignite")].Ranks[0].PointCost=1;
    TestFalse(TEXT("Story path cannot require spendable points"),Bad.Validate(Reason));
    Bad=D;Bad.Skills[TEXT("Fire.Ignite")].Ranks[1].WaterKg=.5;
    TestFalse(TEXT("Mechanic cannot smuggle another resource"),Bad.Validate(Reason));
    Bad=D;Bad.Skills[TEXT("Water.Draw")].LegacyBit=0;
    TestFalse(TEXT("Legacy identity remains unambiguous"),Bad.Validate(Reason));
    Bad=D;Bad.Skills[TEXT("Water.Draw")].Prerequisites.Add({TEXT("Missing"),1});
    TestFalse(TEXT("Missing prerequisite rejected"),Bad.Validate(Reason));
    Bad=D;Bad.Skills[TEXT("Water.Draw")].Prerequisites.Add({TEXT("Fire.Ignite"),1});
    Bad.Skills[TEXT("Fire.Ignite")].Prerequisites.Add({TEXT("Water.Draw"),1});
    TestFalse(TEXT("Cycle rejected"),Bad.Validate(Reason));
    Bad.Skills[TEXT("Fire.Ignite")].Prerequisites.Reset();
    TestTrue(TEXT("Valid dependency graph accepted"),Bad.Validate(Reason));
    FString Json;FFileHelper::LoadFileToString(Json,*(FPaths::ProjectContentDir()/TEXT("AetherCore/Definitions/V10/Skills.json")));
    TestTrue(TEXT("Production JSON reparses"),FAetherSkillDefinitionsV10::Parse(Json,Reason).Validate(Reason));
    TestTrue(TEXT("Malformed JSON yields no partial skill catalog"),FAetherSkillDefinitionsV10::Parse(Json.Left(Json.Len()/2),Reason).Skills.IsEmpty());
    return true;
}
#endif
