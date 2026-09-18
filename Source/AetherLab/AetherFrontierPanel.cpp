#include "AetherFrontierPanel.h"
#include "AetherFrontier.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/Border.h"
#include "Components/VerticalBox.h"
#include "Components/HorizontalBox.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/ScrollBox.h"
#include "Components/ComboBoxString.h"
#include "Components/InputKeySelector.h"
#include "Components/VerticalBoxSlot.h"
#include "EngineUtils.h"
#include "ReactiveWorldSubsystem.h"
void UAetherFrontierViewModel::Refresh(AAetherFrontierCharacter* C)
{
 if(!C||!C->ProfileState())return;const auto& P=C->ProfileState()->Profile;FString Text,Title,First="Close",Second="Close";
 switch(C->Panel)
 {
 case 1:
  Title=TEXT("背包与装备 / Inventory");First=TEXT("装备选中项");Second=TEXT("使用选中项");
  if(!P.Inventory.IsEmpty())C->SelectedItem=FMath::Clamp(C->SelectedItem,0,P.Inventory.Num()-1);
  for(int I=0;I<P.Inventory.Num();++I){const auto& Item=P.Inventory[I];Text+=FString::Printf(TEXT("%s %02d  %s × %d  %s\n"),I==C->SelectedItem?TEXT("> "):TEXT("  "),I+1,*Item.DefinitionId.ToString(),Item.Count,P.Equipped.FindKey(Item.InstanceId)?TEXT("[装备中]"):TEXT(""));}
  Text+=TEXT("\nTab / 下一项：选择    B：拆分    N：合并\n商人旁：Delete 出售；7 购买法力药；8 购买口粮。\n任务物品与训练装备不可出售。满包奖励保留待领。");break;
 case 2:
  Title=TEXT("个人任务 / Journal");First=TEXT("领取待领奖励");Text+=FString::Printf(TEXT("待领取：%d 金币 / %d 材料\n\n"),P.PendingGold,P.PendingMaterial);
  {const auto G=AetherGuide::Resolve(C);Text+=TEXT("当前追踪：")+G.Title+TEXT("\n")+G.Label+TEXT("\n")+G.Hint+TEXT("\nTab / 下一项：切换可进行任务\n\n");}
  for(const auto& Rule:FAetherRules::Get().Quests){const FName Q=Rule.Id;Text+=FString::Printf(TEXT("%s %s%s\n"),P.Claims.Contains(Q)?TEXT("[完成]"):P.Complete(Q)?TEXT("[待领奖]"):P.Available(Q)?TEXT("[进行中]"):TEXT("[未开放]"),Q==C->TrackedQuest?TEXT("> "):TEXT(""),*FAetherProfile::QuestTitle(Q));if(P.Available(Q))for(auto O:FAetherProfile::Objectives(Q))Text+=FString::Printf(TEXT("     %s %s\n"),P.Evidence.Contains(O)?TEXT("✓"):TEXT("·"),*AetherGuide::ObjectiveLabel(O));}
  Text+=FString::Printf(TEXT("\n日常委托日期：%s / 当日完成 %d\n补给：2 份补给；巡逻：3 标记；灭火：3 处委托火。\n三类委托均在城镇公告板开始或结算。"),*P.DailyDate,P.DailyClaims.Num());break;
 case 3:
  Title=TEXT("能力 / Abilities");
  for(int I=0;I<4;++I){const TCHAR* Names[]={TEXT("引焰"),TEXT("引泉"),TEXT("霜凝"),TEXT("雷击")};Text+=FString::Printf(TEXT("%d  %s  %s  法力 %.0f\n"),I+1,Names[I],C->SpellUnlocked(I)?TEXT("已学习"):TEXT("未学习"),UAetherSpellAbility::Cost(I));}
  Text+=TEXT("\n1–4 选择，鼠标中键释放；F 锁定目标。\n回城与导师交互学习满足主线条件的能力。\n控制台：AetherBind Cast F 可重映射施法键并保存。\n菜单底部可选操作并设置按键；冲突按键自动交换。");break;
 case 5:
  Title=TEXT("队伍 / Party");First=TEXT("邀请附近玩家");Second=TEXT("接受邀请");
  Text+=FString::Printf(TEXT("队长：%s\n待接受邀请：%s\n\n"),*C->ProfileState()->PartyLeader,*C->ProfileState()->InvitedBy);
  for(TActorIterator<AAetherFrontierCharacter> It(C->GetWorld());It;++It)if(It->Fighter==EAetherFighter::Player)Text+=FString::Printf(TEXT("%s  HP %.0f / %.0f  %s\n"),It->ProfileState()?*It->ProfileState()->DisplayName:*It->CompanionId.ToString(),It->Health(),It->MaxHealth,It->bCompanionHold?TEXT("等待"):TEXT("行动中"));
  Text+=TEXT("\n4 人上限（真人 + AI）；旅舍可招募砾石、沐禾各一名。\nY 邀请 / U 接受 / O 离队 / H 跟随或等待 / Delete 解散自己的 AI。\n修道院阀门旁 H：指挥同行者执行相同的引导操作。");break;
 case 6:
  Title=TEXT("菜单 / Menu");First=TEXT("保存世界");Second=TEXT("倒地回据点");
  Text=TEXT("菜单打开时在线世界继续运行。\n个人背包、金币、任务事务自动保存；F5 保存世界。\n倒地后可由队友 E 救援，或等待 3 秒按 F8 回据点。\n\nWASD 移动 / Shift 冲刺 / Ctrl 跳跃 / Space 闪避\n左键攻击、按住重击 / 右键格挡\nG 搬运、放下 / C 投掷 / V 推物 / R、T 更换装备\nQ 生命药 / Z 法力药 / E 交互 / F 锁定\n\n当前使用本地开发档案；正式账号服务尚未接入。");break;
 default:Title=TEXT("AetherLab");break;
 }
 Heading=FText::FromString(Title);Body=FText::FromString(Text);PrimaryLabel=FText::FromString(First);SecondaryLabel=FText::FromString(Second);
}
void UAetherFrontierPanel::NativeConstruct(){Super::NativeConstruct();SetIsFocusable(false);}
TSharedRef<SWidget> UAetherFrontierPanel::RebuildWidget()
{
 if(!WidgetTree)WidgetTree=NewObject<UWidgetTree>(this,TEXT("WidgetTree"));
 if(WidgetTree->RootWidget)return Super::RebuildWidget();
 Model=NewObject<UAetherFrontierViewModel>(this);
 auto* Border=WidgetTree->ConstructWidget<UBorder>();Border->SetBrushColor(FLinearColor(.015f,.025f,.04f,.98f));Border->SetPadding(FMargin(24));WidgetTree->RootWidget=Border;
 auto* Box=WidgetTree->ConstructWidget<UVerticalBox>();Border->SetContent(Box);
 Heading=WidgetTree->ConstructWidget<UTextBlock>();Heading->SetColorAndOpacity(FSlateColor(FLinearColor(1,.8f,.4f)));Box->AddChildToVerticalBox(Heading);
 auto* Scroll=WidgetTree->ConstructWidget<UScrollBox>();auto* BodySlot=Box->AddChildToVerticalBox(Scroll);BodySlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));BodySlot->SetPadding(FMargin(0,18));
 Body=WidgetTree->ConstructWidget<UTextBlock>();Body->SetAutoWrapText(true);Scroll->AddChild(Body);
 auto* Row=WidgetTree->ConstructWidget<UHorizontalBox>();Box->AddChildToVerticalBox(Row);
 auto Add=[&](const TCHAR* Label,UTextBlock*& Text){auto* Button=WidgetTree->ConstructWidget<UButton>();Text=WidgetTree->ConstructWidget<UTextBlock>();Text->SetText(FText::FromString(Label));Button->SetContent(Text);Row->AddChildToHorizontalBox(Button);return Button;};
 SettingsRow=WidgetTree->ConstructWidget<UHorizontalBox>();Box->AddChildToVerticalBox(SettingsRow);
 BindingAction=WidgetTree->ConstructWidget<UComboBoxString>();SettingsRow->AddChildToHorizontalBox(BindingAction);
 BindingKey=WidgetTree->ConstructWidget<UInputKeySelector>();BindingKey->SetAllowModifierKeys(false);BindingKey->SetKeySelectionText(FText::FromString(TEXT("请按新键")));SettingsRow->AddChildToHorizontalBox(BindingKey);
 BindingAction->OnSelectionChanged.AddDynamic(this,&UAetherFrontierPanel::ActionSelected);BindingKey->OnKeySelected.AddDynamic(this,&UAetherFrontierPanel::KeySelected);
 UTextBlock* P=nullptr;UTextBlock* S=nullptr;UTextBlock* N=nullptr;UTextBlock* X=nullptr;
 Add(TEXT("操作"),P)->OnClicked.AddDynamic(this,&UAetherFrontierPanel::Primary);PrimaryText=P;
 Add(TEXT("操作"),S)->OnClicked.AddDynamic(this,&UAetherFrontierPanel::Secondary);SecondaryText=S;
 Add(TEXT("  下一项  "),N)->OnClicked.AddDynamic(this,&UAetherFrontierPanel::NextItem);
 Add(TEXT("  关闭  "),X)->OnClicked.AddDynamic(this,&UAetherFrontierPanel::ClosePanel);
 return Super::RebuildWidget();
}
void UAetherFrontierPanel::NativeTick(const FGeometry& G,float Dt)
{
 Super::NativeTick(G,Dt);auto* PC=GetOwningPlayer();auto* C=PC?Cast<AAetherFrontierCharacter>(PC->GetPawn()):nullptr;if(!C)return;
 // Keep the widget visible to tick while its content is transparent; no hidden-widget polling dependency.
 const bool Open=C->bPanel&&C->Panel!=4;SetRenderOpacity(Open?1:0);SetVisibility(Open?ESlateVisibility::Visible:ESlateVisibility::HitTestInvisible);
 SettingsRow->SetVisibility(Open&&C->Panel==6?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
 if(Open&&C->Panel==6&&BindingAction->GetOptionCount()==0)
 {TArray<FName> Names;C->InputActions.GetKeys(Names);Names.Sort(FNameLexicalLess());for(auto Name:Names)if(Name!="LookX"&&Name!="LookY"&&Name!="Escape")BindingAction->AddOption(Name.ToString());BindingAction->SetSelectedOption(TEXT("Cast"));}
 if(Open){Model->Refresh(C);Heading->SetText(Model->Heading);Body->SetText(Model->Body);PrimaryText->SetText(Model->PrimaryLabel);SecondaryText->SetText(Model->SecondaryLabel);SecondaryText->GetParent()->SetVisibility(Model->SecondaryLabel.ToString()==TEXT("Close")?ESlateVisibility::Collapsed:ESlateVisibility::Visible);int32 W,H;PC->GetViewportSize(W,H);SetPositionInViewport(FVector2D(W*.17,H*.16));SetDesiredSizeInViewport(FVector2D(W*.66,H*.65)/FMath::Max(.1f,UWidgetLayoutLibrary::GetViewportScale(this)));}
 if(Open!=WasOpen){WasOpen=Open;PC->bShowMouseCursor=Open;if(Open){FInputModeGameAndUI Mode;Mode.SetHideCursorDuringCapture(false);PC->SetInputMode(Mode);}else PC->SetInputMode(FInputModeGameOnly());}
}
void UAetherFrontierPanel::Primary()
{if(auto* C=Cast<AAetherFrontierCharacter>(GetOwningPlayerPawn())){if(C->Panel==1)C->ServerAction("EquipInstance",C->SelectedItem);else if(C->Panel==2)C->ServerAction("Claim");else if(C->Panel==5)C->ServerAction("Invite");else if(C->Panel==6)C->ServerAction("Save");else ClosePanel();}}
void UAetherFrontierPanel::Secondary()
{if(auto* C=Cast<AAetherFrontierCharacter>(GetOwningPlayerPawn())){if(C->Panel==1)C->ServerAction("UseSelected",C->SelectedItem);else if(C->Panel==5)C->ServerAction("AcceptInvite");else if(C->Panel==6)C->ServerAction("Recover");else ClosePanel();}}
void UAetherFrontierPanel::NextItem(){if(auto* C=Cast<AAetherFrontierCharacter>(GetOwningPlayerPawn()))C->CycleItem();}
void UAetherFrontierPanel::ClosePanel(){if(auto* C=Cast<AAetherFrontierCharacter>(GetOwningPlayerPawn()))C->bPanel=false;}

void UAetherFrontierPanel::ActionSelected(FString Action,ESelectInfo::Type)
{
 if(auto* C=Cast<AAetherFrontierCharacter>(GetOwningPlayerPawn())){UpdatingKey=true;BindingKey->SetSelectedKey(FInputChord(C->BindingFor(*Action)));UpdatingKey=false;}
}
void UAetherFrontierPanel::KeySelected(FInputChord Key)
{
 if(UpdatingKey)return;if(auto* C=Cast<AAetherFrontierCharacter>(GetOwningPlayerPawn()))C->AetherBind(*BindingAction->GetSelectedOption(),Key.Key);
 ActionSelected(BindingAction->GetSelectedOption(),ESelectInfo::Direct);
}
