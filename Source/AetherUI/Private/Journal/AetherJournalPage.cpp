#include "UI/AetherWidgetAssets.h"
#include "Journal/AetherJournalPage.h"
#include "UI/AetherPageWidgets.h"
#include "Characters/AetherFrontierCharacter.h"
#include "Networking/AetherCommandClient.h"
#include "Inventory/AetherNativeInventory.h"
#include "Inspection/AetherInspectionWidgets.h"
#include "Definitions/AetherV10Definitions.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "AetherGuide.h"
#include "World/AetherFrontierState.h"
#include "GameFramework/GameStateBase.h"
using namespace AetherPageWidgets;
TSharedRef<SWidget> UAetherJournalPage::RebuildWidget()
{
    if(!WidgetTree)WidgetTree=NewObject<UWidgetTree>(this);
    if(!WidgetTree->RootWidget)
    {
        auto* Root=WidgetTree->ConstructWidget<UVerticalBox>();WidgetTree->RootWidget=Root;
        auto* Filters=WidgetTree->ConstructWidget<UHorizontalBox>();Root->AddChildToVerticalBox(Filters);
        const TCHAR* Names[]={TEXT("主线任务"),TEXT("日常委托"),TEXT("已完成")};
        for(int32 I=0;I<3;++I)
        {
            auto* Group=WidgetTree->ConstructWidget<UVerticalBox>();Filters->AddChildToHorizontalBox(Group);
            Button(*WidgetTree,*Group,Names[I],FSimpleDelegate::CreateWeakLambda(this,[this,I](){Filter=I;Selected=NAME_None;RefreshPage();}));
        }
        RefreshClock=Text(*WidgetTree,*Root,TEXT("日常委托按服务器 UTC 零点刷新"));
        auto* Body=WidgetTree->ConstructWidget<UHorizontalBox>();Root->AddChildToVerticalBox(Body)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        auto Column=[&](){auto* Scroll=WidgetTree->ConstructWidget<UScrollBox>();Body->AddChildToHorizontalBox(Scroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));auto* Box=WidgetTree->ConstructWidget<UVerticalBox>();Scroll->AddChild(Box);return Box;};
        List=Column();Details=Column();Rewards=Column();
        Notice=Text(*WidgetTree,*Root,TEXT(""));
    }
    return Super::RebuildWidget();
}
UWidget* UAetherJournalPage::InitialFocus() const{return First?First.Get():Super::InitialFocus();}
void UAetherJournalPage::RefreshPage()
{
    if(!List||!Details||!Rewards)return;List->ClearChildren();Rewards->ClearChildren();First=nullptr;
    const auto* P=Profile();if(!P){Details->ClearChildren();Text(*WidgetTree,*Details,TEXT("等待任务同步"));return;}
    const auto& D=FAetherV10Definitions::Get();
    if(Filter==1)
    {
        for(const auto& Daily:D.Rules.Dailies)
        {
            auto* CardRows=Card(*WidgetTree,*List);
            const bool Done=P->DailyClaims.Contains(Daily.Id.ToString());
            Text(*WidgetTree,*CardRows,Daily.Id.ToString()+(Done?TEXT(" · 已完成"):TEXT(" · 可追踪")));
            for(const auto& Fact:Daily.Facts)if(const auto* O=D.Rules.Objectives.Find(Fact))
                Text(*WidgetTree,*CardRows,(P->DailyEvidence.Contains(Fact.ToString())?TEXT("✓ "):TEXT("○ "))+O->Label);
            for(const auto& Cost:Daily.Consume)if(const auto* Item=D.Items.Items.Find(Cost.Key.ToString()))
                Text(*WidgetTree,*CardRows,FString::Printf(TEXT("提交 %s × %d"),*Item->DisplayName,Cost.Value));
            auto* B=Button(*WidgetTree,*CardRows,TEXT("查看委托地点"),FSimpleDelegate::CreateWeakLambda(this,[this,Id=Daily.Id](){Select(Id);}));
            if(!First)First=B;
        }
    }
    else for(const auto& Q:D.Rules.Quests)
    {
        const bool Done=P->Claims.Contains(Q.Id.ToString());if((Filter==2)!=Done)continue;
        bool Available=true;for(FName Pre:Q.Prerequisites)Available&=P->Claims.Contains(Pre.ToString());
        auto* Row=Card(*WidgetTree,*List);
        const FString Status=Done?TEXT("已完成"):Available?TEXT("进行中"):TEXT("未开放");
        auto* B=Button(*WidgetTree,*Row,Q.Title+TEXT(" · ")+Status,FSimpleDelegate::CreateWeakLambda(this,[this,Id=Q.Id](){Select(Id);}));
        if(!First)First=B;if(Selected.IsNone())Selected=Q.Id;
        int32 DoneCount=0;for(FName O:Q.Objectives)DoneCount+=P->Evidence.Contains(O.ToString())?1:0;
        Text(*WidgetTree,*Row,FString::Printf(TEXT("目标 %d / %d"),DoneCount,Q.Objectives.Num()));
    }
    Text(*WidgetTree,*Rewards,TEXT("待领取奖励"),FLinearColor(1,.8f,.4f));
    if(P->PendingRewards.IsEmpty())Text(*WidgetTree,*Rewards,TEXT("没有待领取奖励"));
    for(const auto& Reward:P->PendingRewards)
    {
        auto* R=Card(*WidgetTree,*Rewards);Text(*WidgetTree,*R,Reward.SourceId);
        if(Reward.Gold)Text(*WidgetTree,*R,FString::Printf(TEXT("金币 × %d"),Reward.Gold));
        for(const auto& Pair:Reward.Items)
        {
            const auto* Item=D.Items.Items.Find(Pair.Key);
            Button(*WidgetTree,*R,FString::Printf(TEXT("%s × %d"),Item?*Item->DisplayName:*Pair.Key,Pair.Value),
                FSimpleDelegate::CreateWeakLambda(this,[this,Id=Pair.Key](){Inspect(Id);}));
        }
        Button(*WidgetTree,*R,TEXT("领取可容纳部分"),FSimpleDelegate::CreateWeakLambda(this,[this,Id=Reward.RewardId,Seen=P->Revision](){Claim(Id,Seen);}),!Client->HasPending());
    }
    ShowDetails();LiveRefresh();
}
void UAetherJournalPage::Select(FName Quest){Selected=Quest;ShowDetails();}
void UAetherJournalPage::ShowDetails()
{
    if(!Details)return;Details->ClearChildren();ItemCard=nullptr;const auto* P=Profile();if(!P)return;const auto& D=FAetherV10Definitions::Get();
    const auto* Q=D.Rules.Quest(Selected);
    if(Q)
    {
        Text(*WidgetTree,*Details,Q->Title,FLinearColor(1,.8f,.4f));
        for(FName Id:Q->Objectives)if(const auto* O=D.Rules.Objectives.Find(Id))
        {
            auto* Rows=Card(*WidgetTree,*Details);Text(*WidgetTree,*Rows,(P->Evidence.Contains(Id.ToString())?TEXT("✓ "):TEXT("○ "))+O->Label);
            Text(*WidgetTree,*Rows,O->Hint);
        }
        Text(*WidgetTree,*Details,FString::Printf(TEXT("完成奖励：%d 金币 · %d 经验"),Q->Gold,Q->Experience));
        for(const auto& Item:Q->Items)
        {
            const auto* Def=D.Items.Items.Find(Item.Key.ToString());
            Button(*WidgetTree,*Details,FString::Printf(TEXT("%s × %d"),Def?*Def->DisplayName:*Item.Key.ToString(),Item.Value),
                FSimpleDelegate::CreateWeakLambda(this,[this,Id=Item.Key.ToString()](){Inspect(Id);}));
        }
        Button(*WidgetTree,*Details,TEXT("追踪此任务"),FSimpleDelegate::CreateWeakLambda(this,[this](){if(auto* C=Player()){C->TrackedQuest=Selected;C->OnPresentationChanged.Broadcast();}}),!P->Claims.Contains(Selected.ToString()));
    }
    else if(Filter==1)
    {
        const auto* Daily=D.Rules.Dailies.FindByPredicate([&](const auto& R){return R.Id==Selected;});
        if(Daily)
        {
            Text(*WidgetTree,*Details,TEXT("到城镇公告板开始或结算委托。每日进度由服务器确认。"));
            Text(*WidgetTree,*Details,FString::Printf(TEXT("奖励 %d 金币"),Daily->Gold));
            for(const auto& Item:Daily->Reward)Text(*WidgetTree,*Details,FString::Printf(TEXT("%s × %d"),*Item.Key.ToString(),Item.Value));
            Button(*WidgetTree,*Details,TEXT("打开地图"),FSimpleDelegate::CreateWeakLambda(this,[this](){if(Menu.IsValid())Menu->OpenPage(EAetherMenuPage::Map);}));
        }
    }
}
void UAetherJournalPage::Claim(FGuid Reward,int64 Seen)
{
    auto* C=Player();if(!C)return;FAetherPlayerCommand Command;Command.Type=EAetherCommandType::ClaimReward;
    Command.DefinitionId=TEXT("Reward.")+Reward.ToString(EGuidFormats::Digits);FString Why;
    AetherNativeInventory::Submit(*C,Command,Seen,Why);Notice->SetText(FText::FromString(Why));
}
void UAetherJournalPage::Inspect(const FString& Id)
{
    auto* C=Player();if(!C)return;FAetherInspectionSnapshot S;if(!AetherNativeInventory::Snapshot(*C,Profile()?Profile()->Revision:0,S))return;
    FAetherInspectTarget Target;Target.Kind=EAetherInspectTarget::ItemDefinition;Target.DefinitionId=Id;
    const auto& D=FAetherV10Definitions::Get();
    if(!ItemCard){ItemCard=CreateWidget<UAetherInspectionCard>(GetOwningPlayer(),AetherWidgetAssets::Class<UAetherInspectionCard>());Details->AddChildToVerticalBox(ItemCard);}
    ItemCard->SetModel(AetherInspection::Build(AetherInspection::Pin(S,Target),S,D.Items,D.Skills));
}
void UAetherJournalPage::LiveRefresh()
{
    if(!RefreshClock)return;
    // 日期口径由服务端定义；快照的 DailyDate 只展示已结算日期，客户端时钟不决定领取资格。
    const auto* P=Profile();
    const auto* State=GetWorld()->GetGameState<AAetherFrontierState>();
    FString Clock=TEXT("正在同步服务器刷新时间");
    if(State&&State->DailyResetAt>0)
    {
        const int32 Remaining=FMath::Max(0,FMath::CeilToInt(State->DailyResetAt-State->GetServerWorldTimeSeconds()));
        Clock=FString::Printf(TEXT("UTC 00:00 刷新 · 剩余 %02d:%02d:%02d"),Remaining/3600,(Remaining/60)%60,Remaining%60);
    }
    RefreshClock->SetText(FText::FromString(Clock+TEXT(" · 已记录：")+(P&&!P->DailyDate.IsEmpty()?P->DailyDate:TEXT("尚未参与"))));
}
