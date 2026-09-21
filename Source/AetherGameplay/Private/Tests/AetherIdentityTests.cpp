#include "Misc/AutomationTest.h"
#include "Commands/AetherProfileCoordinator.h"
#include "Commands/AetherContainerCommand.h"
#include "Persistence/AetherSqliteStore.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherExactIdentityTest,"Aether.V10.Identity.ExactOwnershipStackStateAndCanonicalDefinitions",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherExactIdentityTest::RunTest(const FString&)
{
    FString Json,Reason;FFileHelper::LoadFileToString(Json,*(FPaths::ProjectContentDir()/TEXT("AetherCore/Definitions/V10/Items.json")));
    const auto Items=FAetherV10ItemDefinitions::Parse(Json,Reason);const auto& Skills=FAetherSkillDefinitionsV10::Get();const auto& Rules=FAetherRules::Get();
    FAetherV10ItemInstance I;I.InstanceId=FGuid::NewGuid();I.DefinitionId=TEXT("Potion");I.SlotIndex=0;I.Quantity=2;I.StateGroup=TEXT("Wet");I.Affixes.Add(TEXT("Recovery"),2);
    auto Other=I;Other.StateGroup=TEXT("wet");TestFalse(TEXT("Stack state cannot be washed by case folding"),I.SameStackKey(Other));
    Other=I;Other.Affixes.Reset();Other.Affixes.Add(TEXT("recovery"),2);TestFalse(TEXT("Affix key spelling is part of full stack identity"),I.SameStackKey(Other));
    I.BoundToCharacter=TEXT("Alice");Other=I;Other.BoundToCharacter=TEXT("alice");TestFalse(TEXT("Bound characters do not merge by spelling alias"),I.SameStackKey(Other));
    FAetherInventoryStateV10 Inventory;Inventory.Items.Add(I);
    auto Alias=Inventory;Alias.Items[0].DefinitionId=TEXT("potion");TestFalse(TEXT("Persisted instance requires canonical definition ID"),Alias.Validate(Items,Reason));
    TestTrue(TEXT("Grant cannot insert a noncanonical item ID"),Inventory.AddNew(TEXT("potion"),1,Items).Code==EAetherInventoryMutationCode::Missing);
    TestNull(TEXT("Equipment slot lookup is exact"),Items.FindSlot(TEXT("mainhand")));
    FAetherContainerStateV10 Storage;Storage.ContainerId=TEXT("Storage.Alice");Storage.Kind=EAetherContainerKind::PersonalStorage;Storage.OwnerCharacterId=TEXT("Alice");Storage.Inventory=Inventory;
    TestTrue(TEXT("Canonical owner may access personal storage"),Storage.Validate(Items,Reason)&&Storage.Allows(TEXT("Alice")));
    TestFalse(TEXT("Different spelling is not storage ownership"),Storage.Allows(TEXT("alice")));
    Storage.Inventory.Items[0].BoundToCharacter=TEXT("alice");TestFalse(TEXT("Private storage cannot hold another spelling of bound owner"),Storage.Validate(Items,Reason));
    FAetherProfileStateV10 Profile;Profile.CharacterId=TEXT("Alice");Profile.Inventory=Inventory;
    Profile.Claims={TEXT("q_main_02")};TestFalse(TEXT("Profile progression references are canonical"),Profile.Validate(Items,Skills,Rules,Reason));Profile.Claims.Reset();
    FAetherSkillStateV10 SkillState;TestTrue(TEXT("Canonical story grant applies"),SkillState.GrantStory(TEXT("Fire.Ignite"),TEXT("Story.Test"),Skills).Code==EAetherSkillMutationCode::Applied);
    TestTrue(TEXT("Case alias cannot grant another story key"),SkillState.GrantStory(TEXT("fire.ignite"),TEXT("Story.Test"),Skills).Code==EAetherSkillMutationCode::Missing);
    TestNull(TEXT("Skill effect requires canonical ID"),Skills.Effect(TEXT("fire.ignite"),1));
    SkillState.Bind(0,TEXT("Fire.Ignite"),{},Skills);
    TestTrue(TEXT("Hotbar alias cannot short-circuit as unchanged"),SkillState.Bind(0,TEXT("fire.ignite"),{},Skills).Code==EAetherSkillMutationCode::NotAuthorized);
    auto InvalidSkills=Skills;InvalidSkills.Skills[TEXT("Frost.Freeze")].Prerequisites.Add({TEXT("fire.ignite"),1});
    TestFalse(TEXT("Skill prerequisites must reference canonical definition ID"),InvalidSkills.Validate(Reason));
    FFileHelper::LoadFileToString(Json,*(FPaths::ProjectContentDir()/TEXT("AetherCore/Definitions/V10/Economy.json")));
    auto Economy=FAetherEconomyDefinitionsV10::Parse(Json,Items,Reason);Economy.Shops[TEXT("Shop")].Products[0]=TEXT("potion");
    TestFalse(TEXT("Catalog products cannot introduce aliases"),Economy.Validate(Items,Reason));
    FAetherConsumableEffectV10 Recovery;Recovery.DeliveryId=AetherTransactions::NewCommandId(0);Recovery.ItemInstanceId=I.InstanceId;
    Recovery.DefinitionId=TEXT("Potion");Recovery.ProfileRevision=1;Recovery.Before.LifeId=FGuid::NewGuid();Recovery.Before.Health=10;
    Recovery.After=Recovery.Before;Recovery.After.Health=50;Recovery.After.Revision=1;
    FAetherEffectDelivery Delivery;Delivery.Id=Recovery.DeliveryId;Delivery.ActorId=TEXT("alice");AetherConsumableEffects::Encode(Recovery,Delivery.Payload);
    FAetherConsumableReceiver Receiver(TEXT("Alice"),Recovery.Before);
    TestTrue(TEXT("Resource receiver also rejects case-folded owner"),Receiver.Apply(Delivery,TEXT("alice"))==EAetherEffectApplyCode::Invalid&&Receiver.State().Health==10);
    FAetherPlayerCommand Command;Command.Type=EAetherCommandType::SetItemFavorite;Command.ExpectedProfileRevision=0;Command.CommandId=AetherTransactions::NewCommandId(0);Command.ItemInstanceId=I.InstanceId;Command.Enabled=true;
    FAetherProfileCommandContext Context;Context.bCanManageInventory=true;FAetherTransaction T;FAetherCommandResult Result;
    TestFalse(TEXT("Command cannot authorize another spelling of actor"),AetherProfileCommands::Prepare(Command,TEXT("alice"),Profile,Context,Items,Skills,Rules,T,Result));
    TestTrue(TEXT("Identity mismatch is unauthorized"),Result.Code==EAetherCommandCode::Unauthorized);
    Command.ProtocolVersion=AetherCommands::LatestProtocolVersion;Command.ExpectedContainerRevision=0;Command.Type=EAetherCommandType::TransferItem;Command.Enabled=false;Command.Quantity=1;Command.ExpectedWorldRevision=0;Command.TargetStableId=TEXT("chest.a");Command.ContainerId=TEXT("Storage.Alice");
    Context.Container.bAuthorized=Context.Container.bTargetReady=Context.Container.bContainerSession=Context.Container.bCanDeposit=true;
    Context.Container.TargetStableId=TEXT("Chest.A");Context.Container.ContainerId=TEXT("Storage.Alice");FString Key;
    TestTrue(TEXT("Target case alias cannot reuse another interaction session"),AetherContainerCommands::AuthorizeRead(Command,Context,Key)==EAetherCommandCode::OutOfReach);
    // 存储层也复核规范提交者；不能靠上层已检查为理由接受改写另一个键。
    T.ActorId=TEXT("Alice");T.CommandId=AetherTransactions::NewCommandId(-1);T.ExpectedProfileRevision=-1;T.Request={1};
    FAetherAggregateWrite Row;Row.Value.Key={EAetherAggregateKind::Profile,TEXT("alice")};Row.Value.Payload={1};T.Writes.Add(Row);
    TestFalse(TEXT("Case-folded profile write does not satisfy actor aggregate requirement"),AetherTransactions::Validate(T,Reason));
    T.Writes[0].Value.Key.Id=TEXT("Alice");
    FAetherEffectDelivery Effect;Effect.Id=FGuid(0,0,1,1);Effect.ActorId=TEXT("alice");Effect.Payload={1};T.Effects.Add(Effect);
    TestFalse(TEXT("Effect owner must exactly match transaction actor"),AetherTransactions::Validate(T,Reason));T.Effects.Reset();
    FAetherSqliteOptions O;O.DatabasePath=FPaths::ProjectSavedDir()/TEXT("Automation/V10Identity")/FGuid::NewGuid().ToString(EGuidFormats::Digits)/TEXT("state.sqlite");
    auto DB=AetherSQLite::Open(O);if(!TestTrue(TEXT("Open isolated identity store"),DB.Store.IsValid()))return false;
    FAetherProfileCoordinator Coordinator(DB.Store.ToSharedRef(),Items,Skills,Rules);
    const auto Session=Coordinator.BeginSession(TEXT("Alice"));auto Forged=Session;Forged.CharacterId=TEXT("alice");
    TestFalse(TEXT("Session tuple compares exact canonical actor"),Coordinator.IsCurrent(Forged));
    TestFalse(TEXT("Alias cannot replace existing authenticated session"),Coordinator.BeginSession(TEXT("alice")).SessionId.IsValid());
    TestTrue(TEXT("Canonical session remains current"),Coordinator.IsCurrent(Session));
    T.Writes[0].Value.Key.Id=TEXT("alice");
    TestTrue(TEXT("Real store rejects wrong primary profile before queuing"),DB.Store->Commit(T).Get().Code==EAetherStoreCode::Invalid);
    TestTrue(TEXT("Rejected alias did not create either profile"),DB.Store->Read({EAetherAggregateKind::Profile,TEXT("Alice")}).Get().Code==EAetherStoreCode::Missing&&
        DB.Store->Read({EAetherAggregateKind::Profile,TEXT("alice")}).Get().Code==EAetherStoreCode::Missing);
    DB.Store->Close();return true;
}
#endif
