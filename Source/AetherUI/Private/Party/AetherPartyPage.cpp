#include "Party/AetherPartyPage.h"
#include "UI/AetherPageWidgets.h"
#include "AetherFrontier.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "EngineUtils.h"
using namespace AetherPageWidgets;
TSharedRef<SWidget> UAetherPartyPage::RebuildWidget()
{
    if(!WidgetTree)WidgetTree=NewObject<UWidgetTree>(this);
    if(!WidgetTree->RootWidget)
    {
        auto* Root=WidgetTree->ConstructWidget<UVerticalBox>();WidgetTree->RootWidget=Root;
        Invitations=WidgetTree->ConstructWidget<UVerticalBox>();Root->AddChildToVerticalBox(Invitations);
        auto* Body=WidgetTree->ConstructWidget<UHorizontalBox>();Root->AddChildToVerticalBox(Body)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        auto Column=[&](){auto* Scroll=WidgetTree->ConstructWidget<UScrollBox>();Body->AddChildToHorizontalBox(Scroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));auto* Rows=WidgetTree->ConstructWidget<UVerticalBox>();Scroll->AddChild(Rows);return Rows;};
        Members=Column();Nearby=Column();Notice=Text(*WidgetTree,*Root,TEXT(""));
    }
    return Super::RebuildWidget();
}
UWidget* UAetherPartyPage::InitialFocus() const{return First?First.Get():Super::InitialFocus();}
void UAetherPartyPage::Act(FName A,TWeakObjectPtr<AAetherPlayerState> P,TWeakObjectPtr<AAetherFrontierCharacter> B)
{if(auto* C=Player()){C->ServerPartyAction(A,P.Get(),B.Get());Notice->SetText(FText::FromString(TEXT("已请求，等待服务器确认。")));}}
void UAetherPartyPage::RefreshPage()
{
    if(!Members||!Nearby||!Invitations)return;
    Members->ClearChildren();Nearby->ClearChildren();Invitations->ClearChildren();HealthRows.Reset();First=nullptr;
    auto* C=Player();auto* PS=C?C->ProfileState():nullptr;if(!PS)return;
    Text(*WidgetTree,*Members,TEXT("当前队伍"),FLinearColor(1,.8f,.4f));
    Text(*WidgetTree,*Nearby,TEXT("附近玩家 · 邀请范围五米"),FLinearColor(.4f,.8f,1));
    if(!PS->InvitedBy.IsEmpty())
    {
        Text(*WidgetTree,*Invitations,TEXT("收到队伍邀请，三十秒内可接受或拒绝。"));
        First=Button(*WidgetTree,*Invitations,TEXT("接受邀请"),FSimpleDelegate::CreateWeakLambda(this,[this](){Act("AcceptInvite");}));
        Button(*WidgetTree,*Invitations,TEXT("拒绝邀请"),FSimpleDelegate::CreateWeakLambda(this,[this](){Act("DeclineInvite");}));
    }
    const bool Leader=PS->PartyLeader==PS->Profile.CharacterId;int32 Count=0;
    for(TActorIterator<AAetherFrontierCharacter> It(GetWorld());It;++It)
    {
        auto* Pawn=*It;if(Pawn->Fighter!=EAetherFighter::Player)continue;
        auto* State=Pawn->ProfileState();const auto* OwnerState=Pawn->CompanionOwner?Pawn->CompanionOwner->ProfileState():nullptr;
        const bool Member=State?State->PartyLeader==PS->PartyLeader:OwnerState&&OwnerState->PartyLeader==PS->PartyLeader;
        const bool Close=FVector::DistSquared(Pawn->GetActorLocation(),C->GetActorLocation())<=FMath::Square(500.);
        if(!Member&&(!Close||!State))continue;
        auto* CardRows=Card(*WidgetTree,Member?*Members:*Nearby);
        const FString Name=State?State->DisplayName:Pawn->CompanionId=="Muhe"?TEXT("沐禾"):TEXT("砾石");
        Text(*WidgetTree,*CardRows,Name+(State?TEXT(" · 玩家"):TEXT(" · AI"))+(State&&State->bPartyCaptain?TEXT(" · 队长"):TEXT("")));
        auto* Health=Text(*WidgetTree,*CardRows,TEXT(""));HealthRows.Add({Pawn,Health});
        if(Member)
        {
            ++Count;
            if(!State&&Pawn->CompanionOwner==C)
            {
                const TWeakObjectPtr<AAetherFrontierCharacter> Weak=Pawn;
                auto* B=Button(*WidgetTree,*CardRows,Pawn->bCompanionHold?TEXT("跟随"):TEXT("等待"),FSimpleDelegate::CreateWeakLambda(this,[this,Weak](){if(Weak.IsValid())Act("PartyCommand",{},Weak);}));
                if(!First)First=B;
                Button(*WidgetTree,*CardRows,TEXT("解散此同伴"),FSimpleDelegate::CreateWeakLambda(this,[this,Weak](){if(Weak.IsValid())Act("Dismiss",{},Weak);}));
            }
        }
        else if(State)
        {
            const TWeakObjectPtr<AAetherPlayerState> Weak=State;
            auto* B=Button(*WidgetTree,*CardRows,TEXT("邀请此玩家"),FSimpleDelegate::CreateWeakLambda(this,[this,Weak](){if(Weak.IsValid())Act("Invite",Weak);}),Leader);
            if(!First)First=B;
        }
    }
    Text(*WidgetTree,*Members,FString::Printf(TEXT("已显示成员 %d / 4"),Count));
    auto* Leave=Button(*WidgetTree,*Members,TEXT("离开队伍"),FSimpleDelegate::CreateWeakLambda(this,[this](){Act("LeaveParty");}));if(!First)First=Leave;
}
void UAetherPartyPage::LiveRefresh()
{
    auto* C=Player();auto* PS=C?C->ProfileState():nullptr;if(!PS)return;
    FString Key=PS->PartyLeader+TEXT("|")+PS->InvitedBy;
    for(TActorIterator<AAetherFrontierCharacter> It(GetWorld());It;++It)if(It->Fighter==EAetherFighter::Player)
    {
        const auto* S=It->ProfileState();
        Key+=FString::Printf(TEXT("|%s:%d:%u"),S?*S->DisplayName:TEXT(""),S&&S->bPartyCaptain,It->CompanionOwner?It->CompanionOwner->GetUniqueID():0);
        Key+=FString::Printf(TEXT("|%u:%s:%d:%d"),It->GetUniqueID(),S?*S->PartyLeader:TEXT("AI"),It->bCompanionHold,FVector::DistSquared(It->GetActorLocation(),C->GetActorLocation())<=FMath::Square(500.));
    }
    if(Key!=LastRoster){LastRoster=Key;RefreshPage();}
    for(const auto& R:HealthRows)if(R.Pawn.IsValid()&&R.Text.IsValid())
        R.Text->SetText(FText::FromString(FString::Printf(TEXT("生命 %.0f / %.0f · %s"),R.Pawn->Health(),R.Pawn->MaxHealth,
            !R.Pawn->Alive()?TEXT("倒地"):R.Pawn->ReviveTarget?TEXT("正在救援"):R.Pawn->bCompanionHold?TEXT("等待中"):TEXT("行动中"))));
    Notice->SetText(FText::FromString(C->Feedback+(PS->InvitedBy.IsEmpty()?FString():FString::Printf(TEXT("  邀请剩余 %.0f 秒"),FMath::Max(0.f,PS->InvitationExpires-C->CombatTime())))));
}
