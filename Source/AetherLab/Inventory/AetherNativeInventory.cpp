#include "Inventory/AetherNativeInventory.h"
#include "Characters/AetherFrontierCharacter.h"
#include "World/AetherFrontierProp.h"
#include "World/AetherFrontierState.h"
#include "Networking/AetherCommandClient.h"
#include "Definitions/AetherV10Definitions.h"
#include "Contracts/AetherTransaction.h"
#include "Engine/LocalPlayer.h"

namespace
{
UAetherCommandClient* Client(AAetherFrontierCharacter& C)
{
    auto* PC=Cast<APlayerController>(C.GetController());auto* LP=PC?PC->GetLocalPlayer():nullptr;
    return LP?LP->GetSubsystem<UAetherCommandClient>():nullptr;
}
}
bool AetherNativeInventory::Snapshot(AAetherFrontierCharacter& C,int64 Revision,FAetherInspectionSnapshot& Out)
{
    auto* Net=Client(C);if(!Net||!Net->GetProfile().IsSet()||!Net->GetChannel().IsValid())return false;
    const auto& P=Net->GetProfile().GetValue();Out={};Out.Context={P.CharacterId,Net->GetChannel(),Revision};
    Out.Container=Net->GetContainer();Out.ContainerContext=Net->GetContainerContext();Out.ContainerWorldRevision=Net->GetContainerWorldRevision();
    Out.ProfileRevision=P.Revision;Out.Inventory=P.Inventory;Out.Skills=P.Skills;Out.Gold=P.Gold;
    Out.ServerTimeSeconds=C.CombatTime();
    if(const auto* PS=C.ProfileState())
    {
        Out.ExternalGrants=PS->GetNativeSkillGrants();
        for(const auto& G:PS->SkillGrantPresentation)
        {
            if(G.Source!=uint8(EAetherSkillGrantSource::Temporary)||!G.InstanceId.IsValid())continue;
            const auto* D=FAetherV10Definitions::Get().Skills.Skills.Find(G.SkillId);if(!D)continue;
            FAetherInspectStatusEffect Effect;Effect.InstanceId=G.InstanceId;Effect.DefinitionId=G.SkillId;
            Effect.DisplayName=D->DisplayName+TEXT(" · 旅舍祝福");Effect.IconId=D->IconId;Effect.Source=TEXT("旅舍休息");
            Effect.ExpiresAtServerSeconds=G.ExpiresAtServerSeconds;
            Effect.Impacts.Add({TEXT("rank"),TEXT("授权等级"),FString::FromInt(G.Rank)});
            if(const auto* Rank=FAetherV10Definitions::Get().Skills.Effect(G.SkillId,G.Rank))
                for(const auto& Stat:Rank->PassiveStats)Effect.Impacts.Add({Stat.Key,Stat.Key,FString::Printf(TEXT("+%.1f"),Stat.Value)});
            Out.StatusEffects.Add(MoveTemp(Effect));
        }
    }
    Out.bCanAct=C.Ready()&&!C.Carried&&!C.ReviveTarget&&!C.bTravelPending&&!Net->HasPending();
    if(auto* W=C.GetWorld()->GetGameState<AAetherFrontierState>())Out.WorldRevision=W->NativeWorldRevision;
    const auto Shop=C.ActiveShop();const auto* Definition=FAetherV10Definitions::Get().Economy.Shops.Find(Shop.ToString());
    if(!Shop.IsNone()&&Definition&&C.TradeSession.Target.IsValid())
    {Out.Shop=*Definition;Out.TradeTargetStableId=C.TradeSession.Target->Spec.Id.ToString();}
    return true;
}
bool AetherNativeInventory::Submit(AAetherFrontierCharacter& C,FAetherPlayerCommand Command,int64 Seen,FString& Why)
{
    auto* Net=Client(C);
    if(!Net||!Net->GetProfile().IsSet()||Net->GetProfile()->Revision!=Seen||Net->HasPending())
    {Why=TEXT("背包已变化或原请求仍待确认。");return false;}
    Command.ProtocolVersion=AetherCommands::LatestProtocolVersion;Command.ExpectedProfileRevision=Seen;
    Command.CommandId=AetherTransactions::NewCommandId(Seen);TArray<uint8> Bytes;
    if(!AetherCommands::Encode(Command,Bytes,Why))return false;
    if(!Net->Submit(Net->GetChannel(),Net->GetOwnerIdentity(),Bytes,Why))return false;
    Why=TEXT("操作已提交，等待服务器确认。");return true;
}
bool AetherNativeInventory::Shortcut(AAetherFrontierCharacter& C,FName Action,FName Definition,FString& Why)
{
    auto* Net=Client(C);if(!Net||!Net->GetProfile().IsSet()){Why=TEXT("等待原生背包同步。");return false;}
    const auto& P=Net->GetProfile().GetValue();const auto& Defs=FAetherV10Definitions::Get().Items;
    using E=EAetherCommandType;FAetherPlayerCommand Cmd;Cmd.ItemInstanceId=C.SelectedInstance;
    if(Action=="Next")
    {
        TArray<FGuid> Ordered;for(int32 Slot=0;Slot<P.Inventory.Capacity;++Slot)if(const auto* I=P.Inventory.At(Slot))Ordered.Add(I->InstanceId);
        if(Ordered.IsEmpty())return false;C.SelectedInstance=Ordered[(Ordered.Find(C.SelectedInstance)+1)%Ordered.Num()];return true;
    }
    if(Action=="Claim")
    {
        if(P.PendingRewards.IsEmpty()){Why=TEXT("没有待领取的奖励。");return false;}
        Cmd.Type=E::ClaimReward;Cmd.ItemInstanceId.Invalidate();Cmd.DefinitionId=TEXT("Reward.")+P.PendingRewards[0].RewardId.ToString(EGuidFormats::Digits);
        return Submit(C,MoveTemp(Cmd),P.Revision,Why);
    }
    if(Action=="Use")
    {
        Cmd.Type=E::UseItem;
        if(!Definition.IsNone())
        {
            const auto* I=P.Inventory.Items.FindByPredicate([&](const auto& V){return V.DefinitionId==Definition.ToString()&&!V.bLocked&&!P.Inventory.IsEquipped(V.InstanceId);});
            if(!I){Why=TEXT("没有可用的这种道具。");return false;}Cmd.ItemInstanceId=I->InstanceId;
        }
    }
    else if(Action=="CycleMain"||Action=="CycleOff")
    {
        Cmd.SlotId=Action=="CycleMain"?TEXT("MainHand"):TEXT("OffHand");
        const FGuid Current=P.Inventory.Equipment.FindRef(Cmd.SlotId);
        if(Action=="CycleOff"&&Current.IsValid()){Cmd.Type=E::UnequipItem;Cmd.ItemInstanceId=Current;Cmd.SlotId.Reset();}
        else
        {
            TArray<FGuid> Choices;for(int32 Slot=0;Slot<P.Inventory.Capacity;++Slot)
                if(const auto* I=P.Inventory.At(Slot))if(const auto* D=Defs.Items.Find(I->DefinitionId);D&&D->AllowedSlots.Contains(Cmd.SlotId))Choices.Add(I->InstanceId);
            if(Choices.IsEmpty()){Why=TEXT("没有可装备的物品。");return false;}
            Cmd.Type=E::EquipItem;Cmd.ItemInstanceId=Choices[(Choices.Find(Current)+1)%Choices.Num()];
        }
    }
    else if(Action=="Equip")
    {
        const auto* I=P.Inventory.Find(Cmd.ItemInstanceId);const auto* D=I?Defs.Items.Find(I->DefinitionId):nullptr;
        if(!D||D->AllowedSlots.IsEmpty()){Why=TEXT("先选择装备。");return false;}
        if(D->AllowedSlots.Num()!=1){Why=TEXT("请在物品详情中明确选择替换的戒指槽。");return false;}
        Cmd.Type=E::EquipItem;Cmd.SlotId=D->AllowedSlots[0];
    }
    else if(Action=="Unequip")Cmd.Type=E::UnequipItem;
    else if(Action=="Split"){Cmd.Type=E::SplitStack;Cmd.Quantity=C.InventoryQuantity;Cmd.DestinationIndex=P.Inventory.FirstEmpty();}
    else if(Action=="Merge"){Cmd.Type=E::MergeStack;Cmd.OtherInstanceId=C.MergeDestination;Cmd.Quantity=C.InventoryQuantity;}
    else if(Action=="Buy"||Action=="Sell")
    {
        if(C.ActiveShop().IsNone()||!C.TradeSession.Target.IsValid()){Why=TEXT("请先与商人开启交易。");return false;}
        Cmd.TargetStableId=C.TradeSession.Target->Spec.Id.ToString();Cmd.Quantity=C.InventoryQuantity;
        if(Action=="Buy"){Cmd.Type=E::BuyItem;Cmd.DefinitionId=Definition.ToString();Cmd.ItemInstanceId.Invalidate();}
        else Cmd.Type=E::SellItem;
    }
    else {Why=TEXT("不支持该背包快捷操作。");return false;}
    return Submit(C,MoveTemp(Cmd),P.Revision,Why);
}
