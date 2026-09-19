#include "Interaction/AetherLiveTradeSession.h"
#include "Characters/AetherFrontierCharacter.h"
#include "World/AetherFrontierProp.h"
#include "AetherRules.h"
#include "AetherGuide.h"

bool AAetherFrontierCharacter::CanTradeWith(AAetherFrontierProp* Target) const
{
    if(!IsValid(Target)||!ProfileState()||!Ready()||Carried||ReviveTarget||bTravelPending||HasRecentCombat(8))return false;
    const auto Offer=AetherGuide::QueryTarget(const_cast<AAetherFrontierCharacter*>(this),Target);
    return Offer.bExecutable&&Offer.Prop==Target&&FAetherRules::Get().Shops.Contains(Target->Service);
}
FName AAetherFrontierCharacter::ActiveShop() const
{
    const auto* Target=TradeSession.Target.Get();
    if(!ProfileState()||TradeSession.OwnerState!=ProfileState()||!TradeSession.CharacterId.Equals(ProfileState()->Profile.CharacterId,ESearchCase::CaseSensitive)||!TradeSession.Token.IsValid()||!Target||!Target->Service.ToString().Equals(TradeSession.ShopId.ToString(),ESearchCase::CaseSensitive)||
        !CanTradeWith(TradeSession.Target.Get()))return NAME_None;
    return TradeSession.ShopId;
}
bool AAetherFrontierCharacter::AuthorizeTrade(FGuid Token,FName Shop) const
{
    const FName Current=ActiveShop();
    return HasAuthority()&&Token.IsValid()&&Token==TradeSession.Token&&!Current.IsNone()&&
        Current.ToString().Equals(Shop.ToString(),ESearchCase::CaseSensitive);
}
bool AAetherFrontierCharacter::OpenTrade(AAetherFrontierProp* Target)
{
    if(!HasAuthority()||!CanTradeWith(Target))return false;
    // 每次明确与商人交互生成新令牌；即使是同一个 ShopId 的另一个实体也不能沿用旧授权。
    TradeSession={FGuid::NewGuid(),Target,Target->Service,ProfileState()->Profile.CharacterId,ProfileState()};SaleConfirmation={};
    ClientTradeOpened(TradeSession.Token,Target,Target->Service);return true;
}
void AAetherFrontierCharacter::ClientTradeOpened_Implementation(FGuid Token,AAetherFrontierProp* Target,FName Shop)
{
    if(!Token.IsValid()||!IsValid(Target)||!ProfileState())return;
    TradeSession={Token,Target,Shop,ProfileState()->Profile.CharacterId,ProfileState()};SaleConfirmation={};
    bPanel=true;Panel=1;ReleaseHeldInput();
}
void AAetherFrontierCharacter::ClientTradeClosed_Implementation(FGuid Token)
{
    // 延迟到达的旧会话关闭不能关闭后来打开的新商人。
    if(TradeSession.Token!=Token)return;
    TradeSession={};SaleConfirmation={};Feedback=TEXT("交易已关闭。");
}
void AAetherFrontierCharacter::ServerCloseTrade_Implementation(FGuid Token)
{
    if(TradeSession.Token!=Token)return;
    TradeSession={};SaleConfirmation={};ClientTradeClosed(Token);
}
void AAetherFrontierCharacter::CloseTrade()
{
    const auto Token=TradeSession.Token;TradeSession={};SaleConfirmation={};
    if(Token.IsValid()){Feedback=TEXT("交易已关闭。");if(HasAuthority())ClientTradeClosed(Token);else ServerCloseTrade(Token);}
}
void AAetherFrontierCharacter::MaintainTrade()
{
    if(TradeSession.Token.IsValid()&&ActiveShop().IsNone())
    {
        if(HasAuthority())CloseTrade();
        else SaleConfirmation={}; // 客户端即时禁用按钮，授权的最终撤销由服务器发布。
    }
    if(SaleConfirmation.Item.IsValid()&&SaleConfirmationText().IsEmpty()){SaleConfirmation={};Feedback=TEXT("出售确认已取消，请重新选择。");}
}
FString AAetherFrontierCharacter::SaleConfirmationText() const
{
    const auto* PS=ProfileState();const auto& Q=SaleConfirmation;
    if(!PS||!bPanel||Panel!=1||ActiveShop().IsNone()||Q.TradeToken!=TradeSession.Token||
        Q.Item!=SelectedInstance||Q.ProfileRevision!=PS->Profile.Revision||Q.Quantity!=InventoryQuantity||
        CombatTime()>Q.ExpiresAt||PendingInventory.CommandId.IsValid())return {};
    const auto* I=PS->Profile.Inventory.FindByPredicate([&](const auto& V){return V.InstanceId==Q.Item;});
    const auto* R=I?FAetherRules::Get().Items.Find(I->DefinitionId):nullptr;
    if(!I||!R||!R->bRemovable||!R->bSellable||R->Sell<=0||PS->Profile.Equipped.FindKey(Q.Item)||Q.Quantity<1||Q.Quantity>I->Count)return {};
    return FString::Printf(TEXT("确认出售 %s × %d，获得 %lld 金币"),*I->DefinitionId.ToString(),Q.Quantity,int64(R->Sell)*Q.Quantity);
}
void AAetherFrontierCharacter::RequestSale()
{
    if(ActiveShop().IsNone()){Feedback=TEXT("请先与商人交谈，开启交易。");return;}
    if(!SaleConfirmationText().IsEmpty()){SaleConfirmation={};SubmitInventory("Sell");return;}
    const auto* PS=ProfileState();if(!PS)return;
    SaleConfirmation={SelectedInstance,TradeSession.Token,PS->Profile.Revision,InventoryQuantity,CombatTime()+10};
    const auto Text=SaleConfirmationText();
    if(Text.IsEmpty()){SaleConfirmation={};Feedback=TEXT("该物品或数量不可出售；已装备、任务及禁止出售的物品需先检查。");return;}
    Feedback=Text+TEXT("。再次点击出售或按 Delete 确认；10 秒后取消。");
}
