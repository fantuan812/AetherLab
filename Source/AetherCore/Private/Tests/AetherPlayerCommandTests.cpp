#include "Misc/AutomationTest.h"
#include "Contracts/AetherPlayerCommand.h"
#include "Contracts/AetherTransaction.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
FAetherPlayerCommand Example(EAetherCommandType Type)
{
    FAetherPlayerCommand C;C.Type=Type;C.ExpectedProfileRevision=7;
    C.CommandId=FGuid(0,8,0x12345678,0x90ABCDEF);
    using E=EAetherCommandType;
    const auto Item=[&]{C.ItemInstanceId=FGuid(1,2,3,4);};
    const auto Other=[&]{C.OtherInstanceId=FGuid(5,6,7,8);};
    switch(Type)
    {
    case E::UseItem: case E::UnequipItem: Item();break;
    case E::EquipItem: Item();C.SlotId=TEXT("MainHand");break;
    case E::SplitStack: Item();C.Quantity=2;break;
    case E::MergeStack: Item();Other();C.Quantity=2;break;
    case E::BuyItem: C.TargetStableId=TEXT("TownMerchant");C.DefinitionId=TEXT("Potion");C.Quantity=2;break;
    case E::SellItem: Item();C.TargetStableId=TEXT("TownMerchant");C.Quantity=2;break;
    case E::MoveItem: Item();C.DestinationIndex=5;break;
    case E::SwapItems: Item();Other();break;
    case E::SetItemLock: case E::SetItemFavorite: Item();C.Enabled=true;break;
    case E::SortInventory: case E::ResetSkills: break;
    case E::DropItem: Item();C.Quantity=2;C.ExpectedWorldRevision=4;break;
    case E::PickUpItem: Item();C.TargetStableId=TEXT("Loot_1234");C.Quantity=2;C.ExpectedWorldRevision=4;break;
    case E::TransferItem: Item();C.TargetStableId=TEXT("Chest_1234");C.ContainerId=TEXT("TownChest");
        C.Quantity=2;C.ExpectedWorldRevision=4;break;
    case E::RepairItem: Item();C.TargetStableId=TEXT("Smith");break;
    case E::LearnSkill: case E::UpgradeSkill: C.SkillId=TEXT("Fire.Ignite");break;
    case E::BindSkill: C.SkillId=TEXT("Fire.Ignite");C.SlotId=TEXT("Hotbar.1");break;
    case E::ExecuteInteraction: C.TargetStableId=TEXT("Tutor");C.ActionId=TEXT("Train.Fire");C.ExpectedWorldRevision=4;break;
    case E::ClaimReward: C.DefinitionId=TEXT("PendingReward.1234");break;
    default: break;
    }
    return C;
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherCommandWireTest,"Aether.V10.Commands.CanonicalWire",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherCommandWireTest::RunTest(const FString&)
{
    for(uint8 Op=1;Op<=uint8(EAetherCommandType::SetItemFavorite);++Op)
    {
        const auto Command=Example(EAetherCommandType(Op));
        FString Reason;TArray<uint8> Bytes,Again;FAetherPlayerCommand Decoded;
        if(!TestTrue(*FString::Printf(TEXT("Known opcode %d encodes"),Op),AetherCommands::Encode(Command,Bytes,Reason)))continue;
        TestTrue(TEXT("Wire bound"),Bytes.Num()<=AetherCommands::MaxWireBytes);
        if(!TestTrue(*Reason,AetherCommands::Decode(Bytes,Decoded,Reason)))continue;
        TestEqual(TEXT("GUID integer evaluation order is stable"),Decoded.CommandId,Command.CommandId);
        TestEqual(TEXT("Item identity survives wire"),Decoded.ItemInstanceId,Command.ItemInstanceId);
        TestEqual(TEXT("World optional version survives"),Decoded.ExpectedWorldRevision,Command.ExpectedWorldRevision);
        TestTrue(TEXT("Canonical re-encoding"),AetherCommands::Encode(Decoded,Again,Reason)&&Bytes==Again);
        for(int32 Size=0;Size<Bytes.Num();++Size)
        {
            TArray<uint8> Truncated;Truncated.Append(Bytes.GetData(),Size);
            TestFalse(TEXT("Every truncated packet rejected"),AetherCommands::Decode(Truncated,Decoded,Reason));
            TestTrue(TEXT("Failed decode publishes no partial command"),Decoded.Type==EAetherCommandType::Invalid);
        }
        Again=Bytes;Again.Add(0);
        TestFalse(TEXT("Trailing bytes cannot alter receipt identity"),AetherCommands::Decode(Again,Decoded,Reason));
    }
    auto First=Example(EAetherCommandType::BuyItem);auto Changed=First;Changed.Quantity=3;
    TArray<uint8> A,B;FString Reason;
    AetherCommands::Encode(First,A,Reason);AetherCommands::Encode(Changed,B,Reason);
    TestTrue(TEXT("Quantity edits change the durable full request"),A!=B);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherCommandShapeTest,"Aether.V10.Commands.UntrustedShape",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherCommandShapeTest::RunTest(const FString&)
{
    const auto Valid=Example(EAetherCommandType::SellItem);
    auto Reject=[&](FAetherPlayerCommand C,const TCHAR* Label)
    {
        TArray<uint8> Bytes{1,2,3};FString Reason;
        TestFalse(Label,AetherCommands::Encode(C,Bytes,Reason));
        TestTrue(TEXT("Rejected command has diagnostic and no stale bytes"),!Reason.IsEmpty()&&Bytes.IsEmpty());
    };
    auto C=Valid;C.ProtocolVersion=99;Reject(C,TEXT("Future protocol rejected"));
    C=Valid;C.Type=EAetherCommandType(255);Reject(C,TEXT("Unknown opcode rejected"));
    C=Valid;++C.ExpectedProfileRevision;Reject(C,TEXT("Rebinding old ID to fresh revision rejected"));
    C=Valid;C.Quantity=-1;Reject(C,TEXT("Negative quantity rejected"));
    C=Valid;C.Quantity=MAX_int32;Reject(C,TEXT("Overflow quantity rejected"));
    C=Valid;C.TargetStableId=FString::ChrN(97,'a');Reject(C,TEXT("Oversized target rejected"));
    C=Valid;C.TargetStableId=TEXT("../TownMerchant");Reject(C,TEXT("Executable/path syntax is not a stable ID"));
    C=Valid;C.SkillId=TEXT("Unrelated.Skill");Reject(C,TEXT("Unused fields cannot smuggle a second action"));
    C=Valid;C.ExpectedWorldRevision=0;Reject(C,TEXT("Unexpected world dependency rejected"));
    C=Example(EAetherCommandType::TransferItem);C.ExpectedWorldRevision=-1;Reject(C,TEXT("World transfers require expected version"));
    C=Example(EAetherCommandType::MergeStack);C.OtherInstanceId=C.ItemInstanceId;Reject(C,TEXT("Self transfer rejected"));
    C=Example(EAetherCommandType::MoveItem);C.DestinationIndex=256;Reject(C,TEXT("Out of bounds cell rejected"));
    C=Example(EAetherCommandType::UseItem);C.Enabled=true;Reject(C,TEXT("Unexpected flag rejected"));
    TArray<uint8> Bytes;FString Reason;AetherCommands::Encode(Valid,Bytes,Reason);
    Bytes[3]=0xff;FAetherPlayerCommand Out;
    TestFalse(TEXT("Hostile wire version rejected"),AetherCommands::Decode(Bytes,Out,Reason));
    Bytes.SetNumZeroed(AetherCommands::MaxWireBytes+1);
    TestFalse(TEXT("Hostile wire length rejected before allocations"),AetherCommands::Decode(Bytes,Out,Reason));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherCommandResultTest,"Aether.V10.Commands.BoundedResult",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherCommandResultTest::RunTest(const FString&)
{
    FAetherCommandResult R;R.CommandId=FGuid(0,8,1,2);R.Code=EAetherCommandCode::Applied;
    R.FinalProfileRevision=8;R.ActualQuantity=3;R.AffectedIds.Add(FGuid(1,2,3,4));
    R.AffectedDefinitionIds.Add(TEXT("Fire.Ignite"));
    R.ReasonParameters.Add(TEXT("Target"),TEXT("训练导师"));R.ReasonParameters.Add(TEXT("Quantity"),TEXT("3"));
    TArray<uint8> A,B;FString Reason;FAetherCommandResult Out;
    TestTrue(TEXT("Standard result encodes"),AetherCommands::EncodeResult(R,A,Reason));
    TestTrue(TEXT("Standard result decodes"),AetherCommands::DecodeResult(A,Out,Reason));
    TestEqual(TEXT("Chinese display parameter survives"),Out.ReasonParameters.FindRef(TEXT("Target")),FString(TEXT("训练导师")));
    auto Reordered=R;Reordered.ReasonParameters.Reset();
    Reordered.ReasonParameters.Add(TEXT("Quantity"),TEXT("3"));Reordered.ReasonParameters.Add(TEXT("Target"),TEXT("训练导师"));
    TestTrue(TEXT("Map insertion order cannot change stored result"),AetherCommands::EncodeResult(Reordered,B,Reason)&&A==B);
    for(int32 N=0;N<A.Num();++N)
    {
        TArray<uint8> Truncated;Truncated.Append(A.GetData(),N);
        TestFalse(TEXT("Every truncated result rejected"),AetherCommands::DecodeResult(Truncated,Out,Reason));
    }
    // 格式 2 尾部为空转移数组，且实例数量变为 uint16；构造明确的旧格式 1 样本复验兼容。
    auto Legacy=A;Legacy.RemoveAt(Legacy.Num()-2,2);Legacy.RemoveAt(42);Legacy[2]=1;
    TestTrue(TEXT("Frozen result format one remains readable"),AetherCommands::DecodeResult(Legacy,Out,Reason)&&Out.Transfers.IsEmpty()&&Out.ActualQuantity==3);
    auto Transfer=R;Transfer.AffectedIds.Add(FGuid(9,8,7,6));Transfer.Transfers.Add({Transfer.AffectedIds[0],Transfer.AffectedIds[1],3});
    TestTrue(TEXT("Transfer relation persists in result format two"),AetherCommands::EncodeResult(Transfer,B,Reason)&&AetherCommands::DecodeResult(B,Out,Reason)&&Out.Transfers.Num()==1&&Out.Transfers[0].Quantity==3);
    Transfer.ActualQuantity=1001;
    TestTrue(TEXT("Aggregate reward quantity can exceed per-request item quantity"),AetherCommands::EncodeResult(Transfer,B,Reason)&&AetherCommands::DecodeResult(B,Out,Reason)&&Out.ActualQuantity==1001);
    auto TooLarge=Transfer;TooLarge.Transfers.Reset();
    for(int32 I=0;I<512;++I)TooLarge.Transfers.Add({TooLarge.AffectedIds[0],TooLarge.AffectedIds[1],1});
    TestFalse(TEXT("Aggregate reply byte budget still bounds transfer arrays"),AetherCommands::EncodeResult(TooLarge,B,Reason));
    TestTrue(TEXT("Oversized encoded result leaves no stale output"),B.IsEmpty());
    auto BadFormat=A;BadFormat[2]=99;TestFalse(TEXT("Unknown reply format fails closed"),AetherCommands::DecodeResult(BadFormat,Out,Reason));
    auto HugeCount=A;HugeCount[41]=255;HugeCount[42]=255;TestFalse(TEXT("Reply transfer count bounded before allocation"),AetherCommands::DecodeResult(HugeCount,Out,Reason));
    Transfer.Code=EAetherCommandCode::Capacity;Transfer.ActualQuantity=0;
    TestFalse(TEXT("Failure cannot carry successful transfer relations"),AetherCommands::EncodeResult(Transfer,B,Reason));
    R.Code=EAetherCommandCode::Capacity;
    TestFalse(TEXT("Failure cannot report transferred items"),AetherCommands::EncodeResult(R,B,Reason));
    R.ActualQuantity=0;R.ReasonParameters.Add(TEXT("Huge"),FString::ChrN(129,'x'));
    TestFalse(TEXT("Oversized reason rejected"),AetherCommands::EncodeResult(R,B,Reason));
    R.ReasonParameters.Remove(TEXT("Huge"));const auto Duplicate=R.AffectedIds[0];R.AffectedIds.Add(Duplicate);
    TestFalse(TEXT("Duplicate affected identity rejected"),AetherCommands::EncodeResult(R,B,Reason));
    auto C=Example(EAetherCommandType::TransferItem);
    TArray<uint8> Into,From;AetherCommands::Encode(C,Into,Reason);
    C.TransferDirection=EAetherTransferDirection::FromContainer;
    TestTrue(TEXT("Transfer direction is part of the durable request"),AetherCommands::Encode(C,From,Reason)&&Into!=From);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAetherCommandV2Test,"Aether.V10.Commands.V2InteractionRevisionAndV1WireCompatibility",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAetherCommandV2Test::RunTest(const FString&)
{
    FString Reason;
    for(uint8 Type=1;Type<=uint8(EAetherCommandType::SetItemFavorite);++Type)
    {
        auto Legacy=Example(EAetherCommandType(Type));TArray<uint8> V1,V2;FAetherPlayerCommand Decoded;
        if(!TestTrue(TEXT("Encode unchanged v1 command"),AetherCommands::Encode(Legacy,V1,Reason)))return false;
        auto Current=Legacy;Current.ProtocolVersion=2;
        if(Current.Type==EAetherCommandType::ExecuteInteraction)Current.ExpectedInteractionRevision=42;
        if(!TestTrue(TEXT("Encode version 2 command"),AetherCommands::Encode(Current,V2,Reason)))return false;
        TestEqual(TEXT("V2 adds exactly one int64 trailer"),V2.Num(),V1.Num()+8);
        auto Compatible=V2;Compatible.SetNum(V1.Num());Compatible[2]=1;
        TestTrue(TEXT("V1 bytes were not silently redefined"),Compatible==V1);
        TestTrue(TEXT("Version 2 round trip preserves target version"),AetherCommands::Decode(V2,Decoded,Reason)&&Decoded.ProtocolVersion==2&&Decoded.ExpectedInteractionRevision==Current.ExpectedInteractionRevision);
        for(int32 Size=V1.Num();Size<V2.Num();++Size)
        {auto Truncated=V2;Truncated.SetNum(Size);TestFalse(TEXT("Truncated revision suffix rejected"),AetherCommands::Decode(Truncated,Decoded,Reason));}
        TestTrue(TEXT("Old request remains readable with absent target revision"),AetherCommands::Decode(V1,Decoded,Reason)&&Decoded.ProtocolVersion==1&&Decoded.ExpectedInteractionRevision==-1);
    }
    auto Interaction=Example(EAetherCommandType::ExecuteInteraction);Interaction.ProtocolVersion=2;
    TestFalse(TEXT("V2 interaction cannot omit target version"),AetherCommands::Validate(Interaction,Reason));
    Interaction.ExpectedInteractionRevision=0;TestTrue(TEXT("Initial target version is valid"),AetherCommands::Validate(Interaction,Reason));
    Interaction.ProtocolVersion=1;TestFalse(TEXT("V1 cannot smuggle an in-memory target version"),AetherCommands::Validate(Interaction,Reason));
    auto Move=Example(EAetherCommandType::MoveItem);Move.ProtocolVersion=2;Move.ExpectedInteractionRevision=0;
    TestFalse(TEXT("Non-interaction commands must not carry unused target revision"),AetherCommands::Validate(Move,Reason));
    Interaction.ProtocolVersion=2;TArray<uint8> Wire;AetherCommands::Encode(Interaction,Wire,Reason);
    // 最高有效位为 1、但不是 -1 哨兵的无符号值，不能截断成合法正版本。
    Wire.Last()=0x80;FAetherPlayerCommand Output;TestFalse(TEXT("Unsigned target-version overflow rejected"),AetherCommands::Decode(Wire,Output,Reason));
    return true;
}
#endif
