#include "Misc/AutomationTest.h"
#include "Skills/AetherSkillState.h"
#include "Skills/AetherSkillCodec.h"
#if WITH_DEV_AUTOMATION_TESTS
namespace
{
using E=EAetherSkillMutationCode;
FGuid Command(int32 N){return FGuid(0xA37E0011,0,0,N);}
FAetherSkillRuleContext Context()
{
    FAetherSkillRuleContext C;C.bAtResetService=true;
    C.CompletedQuests={TEXT("Q_Main_02"),TEXT("Q_Main_04"),TEXT("Q_Main_05")};return C;
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherSkillLearningTest,"Aether.V10.Skills.PointLedgerLearningAndHistoricalRefund",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherSkillLearningTest::RunTest(const FString&)
{
    const auto& D=FAetherSkillDefinitionsV10::Get();FAetherSkillStateV10 S;FString Reason;auto C=Context();
    const FString Fire=TEXT("Fire.Ignite");
    TestTrue(TEXT("Empty state balances"),S.Validate(D,Reason));
    TestTrue(TEXT("Cannot buy mandatory story base"),S.LearnNext(Fire,Command(1),C,D).Code==E::StoryRequired);
    TestTrue(TEXT("Unique real event grants points"),S.AwardPoints(TEXT("Quest.Q_Main_02"),2,D).Code==E::Applied);
    TestTrue(TEXT("Repeated source is idempotent"),S.AwardPoints(TEXT("Quest.Q_Main_02"),2,D).Code==E::Unchanged);
    TestTrue(TEXT("Same source cannot be rewritten"),S.AwardPoints(TEXT("Quest.Q_Main_02"),3,D).Code==E::Conflict);
    TestEqual(TEXT("No duplicate point award"),S.AvailableSkillPoints,2);
    TestTrue(TEXT("Story grant remains free"),S.GrantStory(Fire,TEXT("Story.Q_Main_02"),D).Code==E::Applied);
    auto MissingQuest=C;MissingQuest.CompletedQuests.Reset();
    TestTrue(TEXT("Upgrade rechecks quest gate"),S.LearnNext(Fire,Command(1),MissingQuest,D).Code==E::QuestRequired);
    TestTrue(TEXT("Next rank deducts its cost"),S.LearnNext(Fire,Command(1),C,D).Code==E::Applied);
    TestEqual(TEXT("Rank really advances"),S.PermanentRank(Fire),2);
    TestEqual(TEXT("Point payment"),S.AvailableSkillPoints,1);
    TestTrue(TEXT("Command ID cannot pay for a second rank"),S.LearnNext(Fire,Command(1),C,D).Code==E::Conflict);
    TestTrue(TEXT("Missing points fails atomically"),S.LearnNext(Fire,Command(2),C,D).Code==E::InsufficientPoints);
    TestEqual(TEXT("Failure keeps rank"),S.PermanentRank(Fire),2);
    S.AwardPoints(TEXT("Level.2"),2,D);
    TestTrue(TEXT("Rank three requires explicit second purchase"),S.LearnNext(Fire,Command(2),C,D).Code==E::Applied);
    TestTrue(TEXT("No fourth rank"),S.LearnNext(Fire,Command(3),C,D).Code==E::MaxRank);
    S.Bind(0,Fire,{},D);
    auto PriceChanged=D;PriceChanged.Skills[Fire].Ranks[1].PointCost=99;PriceChanged.Skills[Fire].Ranks[2].PointCost=100;
    C.bCoolingDown=true;
    TestTrue(TEXT("Reset cannot bypass cooldown"),S.Reset(Fire,C,PriceChanged).Code==E::NotReady);
    TestEqual(TEXT("Rejected reset retains paid ranks"),S.PermanentRank(Fire),3);
    C.bCoolingDown=false;
    const auto Reset=S.Reset(Fire,C,PriceChanged);
    TestTrue(TEXT("Reset succeeds at authorized service"),Reset.Code==E::Applied);
    TestEqual(TEXT("Refund uses historical 1+2, not new 99+100"),Reset.PointsChanged,3);
    TestEqual(TEXT("Lifetime point balance retained"),S.AvailableSkillPoints,4);
    TestEqual(TEXT("Mandatory story base survives"),S.PermanentRank(Fire),1);
    TestTrue(TEXT("Story shortcut remains usable"),S.Hotbar.FindRef(0)==Fire);
    TestTrue(TEXT("Ledger balances after reset"),S.Validate(PriceChanged,Reason));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherSkillSourcesTest,"Aether.V10.Skills.IndependentGrantSourcesAndHotbar",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherSkillSourcesTest::RunTest(const FString&)
{
    const auto& D=FAetherSkillDefinitionsV10::Get();FAetherSkillStateV10 S;FString Reason;
    const FString Fire=TEXT("Fire.Ignite"),Storm=TEXT("Storm.Strike");
    S.GrantStory(Fire,TEXT("Story.Training"),D);
    TArray<FAetherExternalSkillGrant> Grants={
        {TEXT("Item.7"),Fire,3,EAetherSkillGrantSource::Equipment},
        {TEXT("Buff.2"),Fire,2,EAetherSkillGrantSource::Temporary}};
    TestEqual(TEXT("Sources use max rank, not additive rank"),S.EffectiveRank(Fire,Grants),3);
    Grants.RemoveAt(0);TestEqual(TEXT("Unequip leaves temporary grant"),S.EffectiveRank(Fire,Grants),2);
    Grants.Reset();TestEqual(TEXT("Expiration preserves permanent story skill"),S.EffectiveRank(Fire,Grants),1);
    TestTrue(TEXT("Unauthorized hotbar binding refused"),S.Bind(1,Storm,Grants,D).Code==E::NotAuthorized);
    Grants.Add({TEXT("Item.8"),Storm,2,EAetherSkillGrantSource::Equipment});
    TestTrue(TEXT("Equipment active skill can bind"),S.Bind(1,Storm,Grants,D).Code==E::Applied);
    Grants.Reset();
    TestEqual(TEXT("Losing equipment disables effective skill"),S.EffectiveRank(Storm,Grants),0);
    TestTrue(TEXT("Losing equipment keeps recognizable disabled slot"),S.Hotbar.FindRef(1)==Storm&&S.Validate(D,Reason));
    Grants.Add({TEXT("Item.8"),Storm,2,EAetherSkillGrantSource::Equipment});
    TestEqual(TEXT("Reequipping restores same slot authorization"),S.EffectiveRank(S.Hotbar.FindRef(1),Grants),2);
    FAetherSkillStateV10 Empty;
    TArray<FAetherExternalSkillGrant> PassiveGrants={{TEXT("Item.Ward"),TEXT("Storm.Ward"),1,EAetherSkillGrantSource::Equipment}};
    TestTrue(TEXT("Passive skill cannot bind"),Empty.Bind(0,TEXT("Storm.Ward"),PassiveGrants,D).Code==E::NotAuthorized);
    TestTrue(TEXT("Out-of-range slot rejected"),S.Bind(4,Storm,Grants,D).Code==E::Invalid);
    const auto Copy=Grants[0];Grants.Add(Copy);
    TestFalse(TEXT("Same external source cannot duplicate"),FAetherSkillStateV10::ValidateExternalGrants(Grants,D));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherSkillCascadeTest,"Aether.V10.Skills.DependencyResetAndPointInvariants",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherSkillCascadeTest::RunTest(const FString&)
{
    auto D=FAetherSkillDefinitionsV10::Get();const FString Fire=TEXT("Fire.Ignite");
    auto Child=D.Skills[Fire];Child.SkillId=TEXT("Test.Child");Child.bStoryBase=false;Child.LegacyBit=-1;
    Child.RequiredQuest.Reset();Child.Ranks.SetNum(1);Child.Ranks[0].PointCost=3;Child.Prerequisites={{Fire,2}};
    D.Skills.Add(Child.SkillId,Child);
    auto Sibling=Child;Sibling.SkillId=TEXT("Test.Sibling");Sibling.Ranks[0].PointCost=1;Sibling.Prerequisites={{Fire,1}};
    D.Skills.Add(Sibling.SkillId,Sibling);
    FAetherSkillStateV10 S;auto C=Context();FString Reason;
    S.AwardPoints(TEXT("Quest.Test"),10,D);S.GrantStory(Fire,TEXT("Story.Test"),D);
    TestTrue(TEXT("Missing permanent prerequisite refused"),S.LearnNext(Child.SkillId,Command(1),C,D).Code==E::Prerequisite);
    S.LearnNext(Fire,Command(1),C,D);S.LearnNext(Child.SkillId,Command(2),C,D);S.LearnNext(Sibling.SkillId,Command(3),C,D);
    S.Bind(1,Child.SkillId,{},D);S.Bind(2,Sibling.SkillId,{},D);
    const TArray<FAetherExternalSkillGrant> Equipment={{TEXT("Item.9"),Child.SkillId,1,EAetherSkillGrantSource::Equipment}};
    const auto R=S.Reset(Fire,C,D,Equipment);
    TestEqual(TEXT("Cascade refunds only withdrawn paid ranks"),R.PointsChanged,4);
    TestEqual(TEXT("Dependent permanent skill withdrawn"),S.PermanentRank(Child.SkillId),0);
    TestEqual(TEXT("Base-only sibling remains"),S.PermanentRank(Sibling.SkillId),1);
    TestTrue(TEXT("Independent equipment still authorizes child shortcut"),S.Hotbar.FindRef(1)==Child.SkillId&&S.EffectiveRank(Child.SkillId,Equipment)==1);
    TestEqual(TEXT("Balanced remaining points"),S.AvailableSkillPoints,9);
    TestTrue(TEXT("Cascade preserves invariants"),S.Validate(D,Reason));
    auto Bad=S;Bad.AvailableSkillPoints++;
    TestFalse(TEXT("Forged free points rejected"),Bad.Validate(D,Reason));
    Bad=S;Bad.Purchases.Reset();
    TestFalse(TEXT("Learned rank without historical cost rejected"),Bad.Validate(D,Reason));
    auto StoryCycle=D;StoryCycle.Skills[Fire].Prerequisites={{Child.SkillId,1}};
    TestFalse(TEXT("Story foundation cannot rely on paid skills"),StoryCycle.Validate(Reason));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherSkillLegacyTest,"Aether.V10.Skills.LegacyMasksPreserveStoryWithoutRefund",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherSkillLegacyTest::RunTest(const FString&)
{
    const auto& D=FAetherSkillDefinitionsV10::Get();FAetherSkillStateV10 S;FString Reason;auto C=Context();
    for(uint8 Mask=0;Mask<16;++Mask)
    {
        TestTrue(TEXT("Every historical mask converts"),FAetherSkillStateV10::FromLegacyMask(Mask,D,S,Reason));
        for(int32 Bit=0;Bit<4;++Bit)TestEqual(TEXT("Only historical bits authorize base ranks"),S.PermanentRank(D.Legacy(Bit)->SkillId),(Mask&(1<<Bit))?1:0);
        TestTrue(TEXT("Migration invents no payments/points"),S.Purchases.IsEmpty()&&S.PointEvents.IsEmpty()&&S.AvailableSkillPoints==0);
        TestEqual(TEXT("Reset cannot refund historical story learning"),S.Reset({},C,D).PointsChanged,0);
    }
    TestFalse(TEXT("Future bitmask rejected"),FAetherSkillStateV10::FromLegacyMask(16,D,S,Reason));
    TestEqual(TEXT("Failure preserves previous converted state"),S.StoryGrants.Num(),4);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherSkillCodecTest,"Aether.V10.Skills.ExplicitDTOAndCorruptionRejection",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherSkillCodecTest::RunTest(const FString&)
{
    const auto& D=FAetherSkillDefinitionsV10::Get();FAetherSkillStateV10 S,Restored;FString Reason;auto C=Context();
    S.GrantStory(TEXT("Fire.Ignite"),TEXT("Story.Fire"),D);S.GrantStory(TEXT("Water.Draw"),TEXT("Story.Water"),D);
    S.AwardPoints(TEXT("Quest.B"),2,D);S.AwardPoints(TEXT("Quest.A"),3,D);
    S.LearnNext(TEXT("Fire.Ignite"),Command(1),C,D);S.LearnNext(TEXT("Water.Draw"),Command(2),C,D);
    S.Bind(3,TEXT("Water.Draw"),{},D);S.Bind(0,TEXT("Fire.Ignite"),{},D);
    TArray<uint8> Bytes,Again;
    if(!TestTrue(TEXT("Encode complete skill state"),AetherSkillCodec::Encode(S,D,Bytes,Reason)))return false;
    if(!TestTrue(TEXT("Decode complete skill state"),AetherSkillCodec::Decode(Bytes,D,Restored,Reason)))return false;
    TestEqual(TEXT("Remaining points restored"),Restored.AvailableSkillPoints,3);
    TestEqual(TEXT("Paid ranks restored"),Restored.PermanentRank(TEXT("Fire.Ignite")),2);
    TestTrue(TEXT("Story provenance restored"),Restored.StoryGrants.OrderIndependentCompareEqual(S.StoryGrants));
    TestTrue(TEXT("Lifetime point sources restored"),Restored.PointEvents.OrderIndependentCompareEqual(S.PointEvents));
    TestTrue(TEXT("Hotbar identity restored"),Restored.Hotbar.OrderIndependentCompareEqual(S.Hotbar));
    TestTrue(TEXT("Historical command/cost restored"),Restored.Purchases.Num()==2&&Restored.Purchases[0].CommandId==Command(1)&&Restored.Purchases[0].PaidPoints==1);
    auto Ordered=S;Ordered.PointEvents.Reset();Ordered.PointEvents.Add(TEXT("Quest.A"),3);Ordered.PointEvents.Add(TEXT("Quest.B"),2);
    Ordered.Purchases.Swap(0,1);Ordered.Hotbar.Reset();Ordered.Hotbar.Add(0,TEXT("Fire.Ignite"));Ordered.Hotbar.Add(3,TEXT("Water.Draw"));
    TestTrue(TEXT("Encoding is independent of container insertion order"),AetherSkillCodec::Encode(Ordered,D,Again,Reason)&&Bytes==Again);
    for(int32 N=0;N<Bytes.Num();++N)
    {
        TArray<uint8> Cut;Cut.Append(Bytes.GetData(),N);
        TestFalse(TEXT("Every truncation rejected"),AetherSkillCodec::Decode(Cut,D,Restored,Reason));
        TestEqual(TEXT("Decode failure leaves previous valid state"),Restored.AvailableSkillPoints,3);
    }
    Again=Bytes;Again[4]=99;TestFalse(TEXT("Unknown format rejected"),AetherSkillCodec::Decode(Again,D,Restored,Reason));
    Again=Bytes;Again[6]=99;TestFalse(TEXT("Unknown content rejected"),AetherSkillCodec::Decode(Again,D,Restored,Reason));
    Again=Bytes;Again[12]=255;Again[13]=255;TestFalse(TEXT("Forged count rejected before allocation"),AetherSkillCodec::Decode(Again,D,Restored,Reason));
    Again=Bytes;Again[8]++;TestFalse(TEXT("Unbalanced points rejected"),AetherSkillCodec::Decode(Again,D,Restored,Reason));
    Again=Bytes;Again.Add(0);TestFalse(TEXT("Trailing bytes rejected"),AetherSkillCodec::Decode(Again,D,Restored,Reason));
    FAetherSkillStateV10 Empty;
    TestTrue(TEXT("Empty legal profile has an explicit DTO"),AetherSkillCodec::Encode(Empty,D,Again,Reason)&&AetherSkillCodec::Decode(Again,D,Restored,Reason)&&Restored.StoryGrants.IsEmpty());
    return true;
}
#endif
