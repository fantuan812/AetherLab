#include "Misc/AutomationTest.h"
#include "Interaction/AetherInteractionDefinitions.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#if WITH_DEV_AUTOMATION_TESTS
namespace
{
FAetherInteractionDefinitions Definitions(FString& Reason,FAetherEconomyDefinitionsV10& Economy,FString& Json)
{
    FString ItemJson,EconomyJson;const auto Root=FPaths::ProjectContentDir()/TEXT("AetherCore/Definitions/V10/");
    FFileHelper::LoadFileToString(ItemJson,*(Root+TEXT("Items.json")));
    const auto Items=FAetherV10ItemDefinitions::Parse(ItemJson,Reason);
    FFileHelper::LoadFileToString(EconomyJson,*(Root+TEXT("Economy.json")));
    Economy=FAetherEconomyDefinitionsV10::Parse(EconomyJson,Items,Reason);
    FFileHelper::LoadFileToString(Json,*(Root+TEXT("Interactions.json")));
    return FAetherInteractionDefinitions::Parse(Json,FAetherRules::Get(),Economy,Reason);
}
FAetherInteractionSnapshot Snapshot(const FString& Definition)
{
    FAetherInteractionSnapshot S;S.CharacterId=TEXT("Alice");S.TargetStableId=TEXT("NPC.Test");S.DefinitionId=Definition;
    S.ProfileRevision=7;S.WorldRevision=4;S.InteractionRevision=2;S.bLoaded=S.bInRange=S.bLineOfSight=S.bActorCanAct=true;
    for(uint8 I=0;I<=uint8(EAetherInteractionActionKind::ClaimSkillPoints);++I)S.RegisteredHandlers.Add(EAetherInteractionActionKind(I));return S;
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherInteractionDefinitionTest,"Aether.V10.Interaction.BoundedFiniteDialogueDefinitions",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherInteractionDefinitionTest::RunTest(const FString&)
{
    FString Reason,Json;FAetherEconomyDefinitionsV10 Economy;const auto D=Definitions(Reason,Economy,Json);
    if(!TestTrue(*Reason,D.Validate(FAetherRules::Get(),Economy,Reason)))return false;
    TestEqual(TEXT("Seven initial target definitions"),D.Targets.Num(),7);
    auto Bad=D;Bad.Targets[TEXT("Teacher")].Actions[1].RequiredClaims.Add(TEXT("MissingQuest"));
    TestFalse(TEXT("Unknown quest rejected"),Bad.Validate(FAetherRules::Get(),Economy,Reason));
    Bad=D;Bad.Targets[TEXT("Teacher")].Actions[1].RequiredClaims[0]=TEXT("q_main_02");
    TestFalse(TEXT("Case-folded typo is not canonical identity"),Bad.Validate(FAetherRules::Get(),Economy,Reason));
    Bad=D;Bad.Targets[TEXT("Teacher")].Dialogue[TEXT("Greeting")].Options[0].ActionId=TEXT("Exec.Arbitrary");
    TestFalse(TEXT("Dialogue cannot invoke unregistered string script"),Bad.Validate(FAetherRules::Get(),Economy,Reason));
    Bad=D;auto Node=Bad.Targets[TEXT("Teacher")].Dialogue[TEXT("Greeting")];Node.Id=TEXT("Orphan");Bad.Targets[TEXT("Teacher")].Dialogue.Add(Node.Id,Node);
    TestFalse(TEXT("Orphan dialogue rejected"),Bad.Validate(FAetherRules::Get(),Economy,Reason));
    Bad=D;Bad.Targets[TEXT("Shop")].Actions[1].ServiceId=TEXT("UnknownShop");
    TestFalse(TEXT("Shop reference checked against actual catalog"),Bad.Validate(FAetherRules::Get(),Economy,Reason));
    Bad=D;Bad.Targets[TEXT("Teacher")].Actions[1].HideAfterClaims={TEXT("Q_Main_02")};
    TestFalse(TEXT("Impossible condition rejected"),Bad.Validate(FAetherRules::Get(),Economy,Reason));
    const auto Parsed=FAetherInteractionDefinitions::Parse(Json.Replace(TEXT("\"Kind\": \"Talk\""),TEXT("\"Kind\": \"RunScript\"")),FAetherRules::Get(),Economy,Reason);
    TestTrue(TEXT("Unknown action enum rejected while parsing"),Parsed.Targets.IsEmpty());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherInteractionQueryTest,"Aether.V10.Interaction.ReadOnlyOffersTargetIdentityAndGuidance",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherInteractionQueryTest::RunTest(const FString&)
{
    using A=EAetherOfferAvailability;using R=EAetherCommandCode;
    FString Reason,Json;FAetherEconomyDefinitionsV10 Economy;const auto D=Definitions(Reason,Economy,Json);const auto& Rules=FAetherRules::Get();
    if(!TestTrue(*Reason,D.Validate(Rules,Economy,Reason)))return false;
    auto S=Snapshot(TEXT("Teacher"));S.bHasStoryGrantAvailable=true;S.Claims.Add(TEXT("Q_Main_01"));
    const FAetherInteractionQuery Q{S.CharacterId,S.TargetStableId};
    const auto Query=[&](const FAetherInteractionSnapshot& State)
    {return FAetherInteractionProvider(D.Targets[State.DefinitionId],State,Rules).Query(Q);};
    const auto Find=[](const TArray<FAetherInteractionOffer>& Offers,const TCHAR* Id){return Offers.FindByPredicate([&](const auto& O){return O.ActionId==Id;});};
    auto Offers=Query(S);
    TestTrue(TEXT("Locked services do not suppress ordinary conversation"),Find(Offers,TEXT("Talk"))&&Find(Offers,TEXT("Talk"))->Availability==A::TalkOnly);
    TestTrue(TEXT("Training explains public missing prerequisite"),Find(Offers,TEXT("Train"))&&Find(Offers,TEXT("Train"))->ReasonId==TEXT("QuestRequired")&&
        Find(Offers,TEXT("Train"))->ReasonParameters.FindRef(TEXT("QuestId"))==TEXT("Q_Main_02"));
    const auto Guide=AetherInteractionQueries::Guidance(S,Rules);
    TestTrue(TEXT("Unregistered player is guided to registrar"),Guide.QuestId==TEXT("Q_Main_02")&&Guide.ObjectiveId==TEXT("Register")&&Guide.bHasTarget);
    const auto Dialogue=FAetherInteractionProvider(D.Targets[TEXT("Teacher")],S,Rules).QueryDialogue(Q,TEXT("Greeting"));
    TestTrue(TEXT("Locked training appears as disabled dialogue option while conversation remains open"),Dialogue.IsSet()&&
        Dialogue->Choices.ContainsByPredicate([](const auto& V){return V.ActionId==TEXT("Train")&&V.Availability==A::DisabledWithReason;}));
    S.Evidence.Add(TEXT("Register"));const auto InnGuide=AetherInteractionQueries::Guidance(S,Rules);
    TestTrue(TEXT("Registered unbound player is guided to inn"),InnGuide.ObjectiveId==TEXT("Inn")&&InnGuide.bHasTarget&&InnGuide.PersistentPosition==Rules.Objectives[TEXT("Inn")].Position);
    S.Evidence.Add(TEXT("Inn"));TestTrue(TEXT("Completed objectives do not invent a spatial target"),AetherInteractionQueries::Guidance(S,Rules).bRewardReady&&!AetherInteractionQueries::Guidance(S,Rules).bHasTarget);
    S.Claims.Add(TEXT("Q_Main_02"));Offers=Query(S);
    TestTrue(TEXT("Training opens after personal prerequisites"),Find(Offers,TEXT("Train"))&&Find(Offers,TEXT("Train"))->Availability==A::Available);
    FAetherInteractionSelection Selection{S.TargetStableId,TEXT("Train"),S.ProfileRevision,S.WorldRevision,S.InteractionRevision};
    const auto Check=[&](const auto& State,const auto& Choice){return FAetherInteractionProvider(D.Targets[State.DefinitionId],State,Rules).CheckSelection(Q,Choice);};
    TestTrue(TEXT("Exact offered action passes revalidation"),Check(S,Selection)==R::Applied);
    FAetherPlayerCommand Execute;Execute.Type=EAetherCommandType::ExecuteInteraction;Execute.ProtocolVersion=2;
    Execute.CommandId=FGuid(0,8,1,2);Execute.ExpectedProfileRevision=S.ProfileRevision;Execute.ExpectedWorldRevision=S.WorldRevision;
    Execute.ExpectedInteractionRevision=S.InteractionRevision;Execute.TargetStableId=S.TargetStableId;Execute.ActionId=TEXT("Train");
    FAetherInteractionProvider Provider(D.Targets[TEXT("Teacher")],S,Rules);
    TestTrue(TEXT("Typed v2 command checks the complete selection identity"),Provider.CheckCommand(TEXT("Alice"),Execute)==R::Applied);
    Execute.ProtocolVersion=1;Execute.ExpectedInteractionRevision=-1;
    TestTrue(TEXT("Readable legacy interaction cannot authorize new execution without target revision"),Provider.CheckCommand(TEXT("Alice"),Execute)==R::UnsupportedProtocol);
    auto Changed=Selection;Changed.TargetStableId=TEXT("NPC.Adjacent");
    TestTrue(TEXT("Never fall back to a nearby target"),Check(S,Changed)==R::Missing);
    Changed=Selection;Changed.TargetStableId=TEXT("npc.test");
    TestTrue(TEXT("Target identity is exact even though FString equality defaults to case folding"),Check(S,Changed)==R::Missing);
    Changed=Selection;Changed.ActionId=TEXT("train");TestTrue(TEXT("Action identity is also exact"),Check(S,Changed)==R::Missing);
    Changed=Selection;++Changed.InteractionRevision;TestTrue(TEXT("Target replacement invalidates selection"),Check(S,Changed)==R::StaleRevision);
    Changed=Selection;--Changed.ProfileRevision;TestTrue(TEXT("Personal progression changes invalidate selection"),Check(S,Changed)==R::StaleRevision);
    Changed=Selection;--Changed.WorldRevision;TestTrue(TEXT("World changes invalidate selection"),Check(S,Changed)==R::StaleRevision);
    auto State=S;State.bLoaded=false;TestTrue(TEXT("Unloaded target offers no action and cannot execute"),Query(State).IsEmpty()&&Check(State,Selection)==R::NotReady);
    State=S;State.bLineOfSight=false;TestTrue(TEXT("Wall blocks the chosen action"),Check(State,Selection)==R::OutOfReach);
    State=S;State.bBusy=true;TestTrue(TEXT("Busy service cannot execute"),Check(State,Selection)==R::Busy);
    State=S;State.OwnerCharacterId=TEXT("Bob");TestTrue(TEXT("Private targets disclose no offers"),Query(State).IsEmpty()&&Check(State,Selection)==R::Unauthorized);
    State=S;State.RegisteredHandlers.Remove(EAetherInteractionActionKind::Train);TestTrue(TEXT("Unimplemented handler is not advertised as executable"),Check(State,Selection)==R::UnsupportedAction);
    State=S;State.bHasStoryGrantAvailable=false;Offers=Query(State);TestNull(TEXT("Already granted abilities are not offered as new"),Find(Offers,TEXT("LearnStorySkills")));
    State=S;State.DefinitionId=TEXT("Background");TestTrue(TEXT("Background without dialogue has no fake E prompt"),Query(State).IsEmpty());
    auto Secret=D.Targets[TEXT("Teacher")];Secret.Actions[1].bHideLocked=true;State=S;State.Claims.Remove(TEXT("Q_Main_02"));
    Offers=FAetherInteractionProvider(Secret,State,Rules).Query(Q);TestNull(TEXT("Hidden unmet action leaks no reason or quest ID"),Find(Offers,TEXT("LearnStorySkills")));
    State=S;for(const auto& Quest:Rules.Quests)State.Claims.Add(Quest.Id.ToString());
    Offers=Query(State);
    TestTrue(TEXT("After story there is still dialogue and reset service"),Find(Offers,TEXT("Talk"))&&Find(Offers,TEXT("ResetSkills"))&&Find(Offers,TEXT("ResetSkills"))->Availability==A::Available);
    TestNull(TEXT("Completed personal training is not offered again"),Find(Offers,TEXT("Train")));
    TestNull(TEXT("No fake tracked target after all quests complete"),Find(Offers,TEXT("TrackObjective")));
    const auto EndDialogue=FAetherInteractionProvider(D.Targets[TEXT("Teacher")],State,Rules).QueryDialogue(Q,TEXT("Greeting"));
    TestTrue(TEXT("Dialogue omits completed training and missing guide actions"),EndDialogue.IsSet()&&
        !EndDialogue->Choices.ContainsByPredicate([](const auto& V){return V.ActionId==TEXT("Train")||V.ActionId==TEXT("TrackObjective");}));
    auto OutOfRange=S;OutOfRange.bInRange=false;
    TestFalse(TEXT("Direct node ID cannot bypass range check"),FAetherInteractionProvider(D.Targets[TEXT("Teacher")],OutOfRange,Rules).QueryDialogue(Q,TEXT("Greeting")).IsSet());
    auto Hidden=S;Hidden.Claims.Remove(TEXT("Q_Main_02"));
    const auto HiddenDialogue=FAetherInteractionProvider(Secret,Hidden,Rules).QueryDialogue(Q,TEXT("Greeting"));
    TestTrue(TEXT("Hidden action is also omitted from dialogue options"),HiddenDialogue.IsSet()&&
        !HiddenDialogue->Choices.ContainsByPredicate([](const auto& V){return V.ActionId==TEXT("LearnStorySkills");}));
    TestTrue(TEXT("Queries did not mutate original personal evidence or claims"),S.Claims.Num()==2&&S.Evidence.Num()==2);
    // 不同玩家使用独立已提交事实，不能从全局世界“已有人登记”推断个人完成。
    State=S;State.CharacterId=TEXT("Bob");State.Claims.Reset();State.Evidence.Reset();
    const auto Bob=FAetherInteractionProvider(D.Targets[TEXT("Teacher")],State,Rules).Query({TEXT("Bob"),State.TargetStableId});
    TestTrue(TEXT("Second player retains own locked training"),Find(Bob,TEXT("Train"))&&Find(Bob,TEXT("Train"))->Availability==A::DisabledWithReason);
    return true;
}
#endif
