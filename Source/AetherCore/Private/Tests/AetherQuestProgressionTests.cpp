#include "Misc/AutomationTest.h"
#include "Quests/AetherQuestProgression.h"
#include "Profile/AetherProfileCodec.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#if WITH_DEV_AUTOMATION_TESTS
namespace
{
struct FDefinitions
{
    FAetherV10ItemDefinitions Items;FAetherQuestProgressionDefinitions Progression;
    const FAetherRules& Rules=FAetherRules::Get();const FAetherSkillDefinitionsV10& Skills=FAetherSkillDefinitionsV10::Get();
    FDefinitions()
    {
        FString Json,Reason;const auto Root=FPaths::ProjectContentDir()/TEXT("AetherCore/Definitions/V10/");
        FFileHelper::LoadFileToString(Json,*(Root+TEXT("Items.json")));Items=FAetherV10ItemDefinitions::Parse(Json,Reason);
        FFileHelper::LoadFileToString(Json,*(Root+TEXT("Progression.json")));Progression=FAetherQuestProgressionDefinitions::Parse(Json,Rules,Reason);
    }
    TArray<uint8> Bytes(const FAetherProfileStateV10& P) const
    {TArray<uint8> B;FString R;AetherProfileCodec::Encode(P,Items,Skills,Rules,B,R);return B;}
    FAetherQuestMutation Settle(FAetherProfileStateV10& P,const TMap<FString,FString>& Facts={},const FString& Manual={}) const
    {return AetherQuestProgression::Settle(P,Facts,Manual,Items,Skills,Rules,Progression);}
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherQuestSettlementTest,"Aether.V10.Quests.AtomicSettlementDeferredRewardsAndWorldEvidence",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherQuestSettlementTest::RunTest(const FString&)
{
    using E=EAetherQuestMutationCode;FDefinitions D;FString Reason;
    if(!TestTrue(*Reason,D.Progression.Validate(D.Rules,Reason)))return false;
    FAetherProfileStateV10 P;P.CharacterId=TEXT("Alice");
    TestFalse(TEXT("Early training cannot invent an unavailable objective"),AetherQuestProgression::Observe(P,TEXT("Melee1"),D.Rules));
    for(const auto* Fact:{TEXT("SupplyA"),TEXT("SupplyB"),TEXT("Gate")})TestTrue(TEXT("Server evidence records available personal objective"),AetherQuestProgression::Observe(P,Fact,D.Rules));
    auto Result=D.Settle(P);TestTrue(TEXT("First quest settles currency XP and claim together"),Result.Code==E::Applied&&P.Gold==40&&P.Experience==100&&P.Claims.Contains(TEXT("Q_Main_01"))&&P.Skills.AvailableSkillPoints==0);
    TestTrue(TEXT("Duplicate completion grants nothing"),D.Settle(P).Code==E::Unchanged&&P.Gold==40);
    for(int32 Cell=0;Cell<32;++Cell)
    {
        FAetherV10ItemInstance I;I.InstanceId=FGuid::NewGuid();I.DefinitionId=TEXT("Potion");I.Quantity=20;I.SlotIndex=Cell;P.Inventory.Items.Add(I);
    }
    AetherQuestProgression::Observe(P,TEXT("Register"),D.Rules);AetherQuestProgression::Observe(P,TEXT("Inn"),D.Rules);
    const auto BeforeBinding=P;Result=D.Settle(P);
    TestTrue(TEXT("Full inventory defers entire checkpoint reward without blocking story"),Result.Code==E::Applied&&P.bRegistered&&P.Claims.Contains(TEXT("Q_Main_02"))&&P.Experience==200&&P.Gold==40&&P.PendingRewards.Num()==1);
    if(!TestEqual(TEXT("One deferred reward ID"),Result.DeferredRewards.Num(),1))return false;
    TestTrue(TEXT("Pending reward retains all gold and three equipment definitions"),P.PendingRewards[0].Gold==40&&P.PendingRewards[0].Items.Num()==3&&P.Inventory.Items.Num()==32&&P.PendingRewards[0].SourceId==TEXT("Quest.Q_Main_02"));
    const auto PendingId=P.PendingRewards[0].RewardId;D.Settle(P);
    TestTrue(TEXT("Repeating settlement does not duplicate pending reward"),P.PendingRewards.Num()==1&&P.PendingRewards[0].RewardId==PendingId);
    for(const auto* Fact:{TEXT("Melee1"),TEXT("Melee2"),TEXT("Melee3"),TEXT("Block"),TEXT("TrainingExtinguished")})AetherQuestProgression::Observe(P,Fact,D.Rules);
    Result=D.Settle(P);
    TestTrue(TEXT("Ordinary story completion awards actual spendable points"),Result.Code==E::Applied&&Result.AwardedSkillPoints==2&&P.Skills.AvailableSkillPoints==2&&P.Skills.PointEvents.FindRef(TEXT("Quest.Q_Main_03"))==2&&P.Experience==300);
    const auto& FactRule=D.Rules.Objectives[TEXT("SupplyRestored")];
    if(!TestTrue(TEXT("Production pump fact has retroactive provenance"),FactRule.bRetroactive&&!FactRule.FactSources.IsEmpty()))return false;
    TMap<FString,FString> WorldFacts{{TEXT("SupplyRestored"),FactRule.FactSources[0].ToString()}};
    Result=D.Settle(P,WorldFacts);
    TestTrue(TEXT("Validated world fact settles newly open story once"),Result.Code==E::Applied&&P.Claims.Contains(TEXT("Q_Main_05"))&&P.Skills.AvailableSkillPoints==4&&P.PendingRewards.Num()==2);
    const auto Stable=D.Bytes(P);WorldFacts[TEXT("SupplyRestored")]=TEXT("UntrustedSource");
    TestTrue(TEXT("Forged world provenance cannot alter profile"),D.Settle(P,WorldFacts).Code==E::Invalid&&D.Bytes(P)==Stable);
    TestTrue(TEXT("Unavailable manual quest is atomic rejection"),D.Settle(P,{},TEXT("Q_Main_08")).Code==E::NotAllowed&&D.Bytes(P)==Stable);
    auto FullPending=BeforeBinding;
    for(int32 I=0;I<128;++I){FAetherPendingRewardV10 R;R.RewardId=FGuid::NewGuid();R.SourceId=TEXT("Test.Reward");R.Gold=1;FullPending.PendingRewards.Add(R);}
    const auto CapacityBefore=D.Bytes(FullPending);
    TestTrue(TEXT("Pending capacity exhaustion rolls back claim XP inventory and flags"),D.Settle(FullPending).Code==E::Capacity&&D.Bytes(FullPending)==CapacityBefore&&!FullPending.bRegistered);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherQuestPointSourceTest,"Aether.V10.Quests.HistoricalPointSourcesAndCapacity",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherQuestPointSourceTest::RunTest(const FString&)
{
    using E=EAetherQuestMutationCode;FDefinitions D;FString Reason;
    auto Invalid=D.Progression;Invalid.QuestSkillPoints.Remove(TEXT("Q_Main_02"));TestFalse(TEXT("Point config covers all quests explicitly"),Invalid.Validate(D.Rules,Reason));
    Invalid=D.Progression;Invalid.QuestSkillPoints.Add(TEXT("UnknownQuest"),2);TestFalse(TEXT("Unknown quest point source rejected"),Invalid.Validate(D.Rules,Reason));
    FAetherProfileStateV10 P;P.CharacterId=TEXT("LegacyPlayer");
    for(const auto& Q:D.Rules.Quests)P.Claims.Add(Q.Id.ToString());
    FAetherSkillStateV10::FromLegacyMask(15,D.Skills,P.Skills,Reason);
    TestTrue(TEXT("Legacy ability conversion itself invents no paid history"),P.Skills.PointEvents.IsEmpty()&&P.Skills.Purchases.IsEmpty());
    const auto Claim=[&](){return AetherQuestProgression::ClaimSkillPoints(P,D.Items,D.Skills,D.Rules,D.Progression);};
    TestTrue(TEXT("Completed quests expose explicit v10 point claim"),AetherQuestProgression::HasClaimableSkillPoints(P,D.Progression));
    auto Result=Claim();
    TestTrue(TEXT("Explicit claim awards six unique two-point sources"),Result.Code==E::Applied&&Result.AwardedSkillPoints==12&&P.Skills.AvailableSkillPoints==12&&P.Skills.PointEvents.Num()==6&&P.Skills.Purchases.IsEmpty()&&P.Gold==0&&P.Experience==0);
    const auto Claimed=D.Bytes(P);TestTrue(TEXT("Repeat claim cannot issue points again"),Claim().Code==E::Unchanged&&D.Bytes(P)==Claimed&&!AetherQuestProgression::HasClaimableSkillPoints(P,D.Progression));
    for(auto& Point:D.Progression.QuestSkillPoints)if(Point.Value>0)Point.Value=3;
    TestTrue(TEXT("Balance change does not rewrite historical grant values"),Claim().Code==E::Unchanged&&P.Skills.AvailableSkillPoints==12&&D.Bytes(P)==Claimed);
    P.Skills={};P.Claims={TEXT("Q_Main_03")};
    for(int32 I=0;I<FAetherSkillStateV10::MaxPointEvents;++I)P.Skills.PointEvents.Add(FString::Printf(TEXT("Old.%d"),I),1);
    P.Skills.AvailableSkillPoints=FAetherSkillStateV10::MaxPointEvents;
    const auto Full=D.Bytes(P);TestTrue(TEXT("Full source ledger cannot partially grant"),Claim().Code==E::Capacity&&D.Bytes(P)==Full);
    return true;
}
#endif
