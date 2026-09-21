#include "Persistence/AetherLegacyProfileConverter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
namespace
{
const FAetherRules& FrozenRules()
{
    static const FAetherRules D=[]
    {
        FString Json;FFileHelper::LoadFileToString(Json,*(FPaths::ProjectContentDir()/TEXT("AetherCore/Definitions/Legacy/V9Rules.json")));
        return FAetherRules::Parse(Json);
    }();return D;
}
FString Text(FName Name){return Name.IsNone()?FString():Name.ToString();}
template<class T> FString Canonical(FName Name,const TMap<FString,T>& Definitions)
{
    FString Found;
    // 旧 FName 是不区分大小写的 ID；只接受唯一对应，不能在新定义中任选一个。
    for(const auto& P:Definitions)if(Name==FName(*P.Key)){if(!Found.IsEmpty())return {};Found=P.Key;}
    return Found;
}
FGuid PendingId(const FString& Digest,const FString& Character)
{
    const FString Key=TEXT("Aether.V10.Legacy.Pending.")+Digest+TEXT(".")+Character;
    FTCHARToUTF8 Bytes(*Key);FMD5 Hash;uint8 Raw[16];
    Hash.Update(reinterpret_cast<const uint8*>(Bytes.Get()),Bytes.Length());Hash.Final(Raw);
    const auto Word=[&](int32 I){return uint32(Raw[I])|(uint32(Raw[I+1])<<8)|(uint32(Raw[I+2])<<16)|(uint32(Raw[I+3])<<24);};
    return FGuid(Word(0),Word(4),Word(8),Word(12));
}
}
bool AetherLegacyV9::ConvertProfile(const FAetherProfile& Old,int32 Schema,const FString& Digest,
    const FAetherV10ItemDefinitions& Items,const FAetherSkillDefinitionsV10& Skills,const FAetherRules& Rules,
    FAetherProfileStateV10& Out,FString& Reason)
{
    const auto Fail=[&](const TCHAR* Why){Reason=Why;return false;};
    if(!FrozenRules().bValid||!Old.Validate(FrozenRules()))return Fail(TEXT("Legacy profile violates frozen v9 rules"));
    if(!Items.Validate(Reason)||!Skills.Validate(Reason))return false;
    FAetherProfileStateV10 Next;
    Next.CharacterId=Old.CharacterId;Next.Revision=Old.Revision;Next.Gold=Old.Gold;Next.Experience=Old.Experience;
    Next.bRegistered=Old.bRegistered;Next.bCompanion=Old.bCompanion;
    Next.LastAbbeyReceipt=Old.LastAbbeyReceipt;Next.LastRelayReceipt=Old.LastRelayReceipt;
    Next.LegacySaveSchema=Schema;Next.LegacySourceSha256=Digest;Next.LegacyProfileRevision=Old.Revision;
    Next.Inventory.Capacity=Items.DefaultCapacity;
    if(Old.Inventory.Num()>Next.Inventory.Capacity)return Fail(TEXT("New capacity cannot hold all legacy instances"));
    for(int32 Index=0;Index<Old.Inventory.Num();++Index)
    {
        const auto& Entry=Old.Inventory[Index];FAetherV10ItemInstance I;
        I.InstanceId=Entry.InstanceId;I.DefinitionId=Canonical(Entry.DefinitionId,Items.Items);
        const auto* Definition=Items.Items.Find(I.DefinitionId);
        if(!Definition)return Fail(TEXT("Unknown/ambiguous legacy item; original instance must not be dropped"));
        I.Quantity=Entry.Count;I.SlotIndex=Index;
        // 旧档没有耐久损耗记录。只为新定义明确启用的物品初始化满耐久，不猜测损坏。
        I.Durability=Definition->MaxDurability>0?Definition->MaxDurability:-1;
        Next.Inventory.Items.Add(MoveTemp(I));
    }
    for(const auto& Pair:Old.Equipped)
    {
        FString Slot;
        for(const auto& Candidate:Items.Slots)if(Pair.Key==FName(*Candidate.Id))
        {if(!Slot.IsEmpty())return Fail(TEXT("Ambiguous legacy slot"));Slot=Candidate.Id;}
        if(Slot.IsEmpty())return Fail(TEXT("Unknown legacy equipment slot"));
        Next.Inventory.Equipment.Add(Slot,Pair.Value);
    }
    if(!FAetherSkillStateV10::FromLegacyMask(Old.LearnedSpells,Skills,Next.Skills,Reason))return false;
    // FName 的旧引用不区分大小写；转成区分大小写的持久 ID 时使用规范定义的拼写。
    TMap<FName,FString> Facts,Quests,DailyFacts,Dailies;
    for(const auto& P:Rules.Objectives)Facts.Add(P.Key,P.Key.ToString());
    for(const auto& Q:Rules.Quests)Quests.Add(Q.Id,Q.Id.ToString());
    for(const auto& D:Rules.Dailies){Dailies.Add(D.Id,D.Id.ToString());for(FName F:D.Facts)DailyFacts.Add(F,F.ToString());}
    const auto Copy=[](const TArray<FName>& From,TArray<FString>& To,const TMap<FName,FString>& Catalog)
    {
        for(FName V:From){const auto* CanonicalId=Catalog.Find(V);if(!CanonicalId)return false;To.Add(*CanonicalId);}
        return true;
    };
    if(!Copy(Old.Evidence,Next.Evidence,Facts)||!Copy(Old.Claims,Next.Claims,Quests)||
        !Copy(Old.DailyEvidence,Next.DailyEvidence,DailyFacts)||!Copy(Old.DailyClaims,Next.DailyClaims,Dailies))
        return Fail(TEXT("Unknown legacy progression ID"));
    Next.DailyDate=Old.DailyDate;
    if(Old.PendingGold>0||Old.PendingMaterial>0)
    {
        FAetherPendingRewardV10 P;P.RewardId=PendingId(Digest,Old.CharacterId);P.SourceId=TEXT("Legacy.V9.Pending");P.Gold=Old.PendingGold;
        if(Old.PendingMaterial>0)P.Items.Add(TEXT("Material"),Old.PendingMaterial);
        Next.PendingRewards.Add(MoveTemp(P));
    }
    for(const auto& Receipt:Old.InventoryReceipts)
    {
        FAetherLegacyInventoryReceiptV9 P;
        P.CommandId=Receipt.Command.CommandId;P.ItemInstanceId=Receipt.Command.ItemInstanceId;
        P.DestinationInstanceId=Receipt.Command.DestinationInstanceId;P.ExpectedRevision=Receipt.Command.ExpectedInventoryRevision;
        P.FinalRevision=Receipt.FinalRevision;P.Quantity=Receipt.Command.Quantity;P.Transferred=Receipt.Transferred;
        P.Action=Text(Receipt.Command.Action);P.DefinitionId=Text(Receipt.Command.DefinitionId);P.ShopId=Text(Receipt.Command.ShopId);
        Next.LegacyInventoryReceipts.Add(MoveTemp(P));
    }
    if(!Next.Validate(Items,Skills,Rules,Reason))return false;
    // 唯一发布点；未知物品、容量不足或任一字段不合法时都保留旧输入和调用方输出。
    Out=MoveTemp(Next);return true;
}
