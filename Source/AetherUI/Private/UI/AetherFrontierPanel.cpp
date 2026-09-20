#include "AetherFrontierPanel.h"
#include "AetherGuide.h"
#include "AetherFrontier.h"
#include "Blueprint/WidgetTree.h"
#include "Engine/LocalPlayer.h"
#include "TimerManager.h"
#include "Components/Border.h"
#include "Components/SpinBox.h"
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
 if(!C||!C->ProfileState())return;++RefreshCount;const auto& P=C->ProfileState()->Profile;FString Text,Title,First="Close",Second="Close";
 switch(C->Panel)
 {
 case 1:
  Title=TEXT("背包与装备 / Inventory");First=TEXT("装备选中项");Second=TEXT("使用选中项");
  if(!C->SelectedInstance.IsValid()&&!P.Inventory.IsEmpty())C->SelectedInstance=P.Inventory[0].InstanceId;
  C->SelectedItem=P.Inventory.IndexOfByPredicate([&](const auto& Item){return Item.InstanceId==C->SelectedInstance;});
  for(int I=0;I<P.Inventory.Num();++I){const auto& Item=P.Inventory[I];Text+=FString::Printf(TEXT("%s %02d  %s × %d  %s\n"),I==C->SelectedItem?TEXT("> "):TEXT("  "),I+1,*Item.DefinitionId.ToString(),Item.Count,P.Equipped.FindKey(Item.InstanceId)?TEXT("[装备中]"):TEXT(""));}
  Text+=TEXT("\nTab / 下一项：选择    B：拆分    N：合并\n与商人交谈后开启交易：Delete 出售需再次确认；7 / 8 购买药水和口粮。\n任务物品与训练装备不可出售。满包奖励保留待领。");break;
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
  Text=TEXT("菜单打开时在线世界继续运行。\n个人背包、金币、任务事务自动保存；F5 保存世界。\n倒地后可由队友 E 救援，或等待 3 秒按 F8 回据点。\n\nWASD 移动 / Shift 冲刺 / Space 跳跃 / Ctrl 下蹲 / Alt 闪避\n左键攻击、按住重击 / 右键格挡\nG 搬运、放下 / C 投掷 / V 推物 / R、T 更换装备\nQ 生命药 / Z 法力药 / E 交互 / F 锁定\n\n当前使用本地开发档案；正式账号服务尚未接入。");break;
 default:Title=TEXT("AetherLab");break;
 }
 Heading=FText::FromString(Title);Body=FText::FromString(Text);PrimaryLabel=FText::FromString(First);SecondaryLabel=FText::FromString(Second);
}
void UAetherFrontierPanel::NativeConstruct()
{
 Super::NativeConstruct();SetIsFocusable(false);
 // 相对锚点交给 Slate/UMG 布局，分辨率或 DPI 变化不需要每帧重写像素坐标。
 SetDesiredSizeInViewport(FVector2D::ZeroVector);SetPositionInViewport(FVector2D::ZeroVector,false);
 SetAnchorsInViewport(FAnchors(.12f,.12f,.88f,.86f));
 if(auto* LP=GetOwningLocalPlayer())
 {
  Menu=LP->GetSubsystem<UAetherMenuSubsystem>();
  Menu->OnChanged.AddUObject(this,&UAetherFrontierPanel::HandleMenuChanged);
 }
 HandleMenuChanged();
}
void UAetherFrontierPanel::NativeDestruct()
{
 SavePageMemory();
 if(Menu.IsValid())Menu->OnChanged.RemoveAll(this);
 if(BoundCharacter.IsValid())BoundCharacter->OnPresentationChanged.RemoveAll(this);
 if(BoundProfile.IsValid())BoundProfile->OnProfilePublished.RemoveAll(this);
 if(GetWorld())GetWorld()->GetTimerManager().ClearTimer(LiveDetailsTimer);
 BoundCharacter.Reset();BoundProfile.Reset();Menu.Reset();Super::NativeDestruct();
}
void UAetherFrontierPanel::SavePageMemory()
{
 // 换 Pawn 时子系统已清空页面记忆，不能用旧 Widget 的缓存把它重新写回去。
 if(!Menu.IsValid()||!BoundCharacter.IsValid()||Menu->GetBoundPawn()!=BoundCharacter.Get()||ShownPage==EAetherMenuPage::None)return;
 auto Memory=Menu->GetPageMemory(ShownPage);
 if(BodyScroll)Memory.ScrollOffset=BodyScroll->GetScrollOffset();
 Memory.SelectedInstance=BoundCharacter->SelectedInstance;Menu->SavePageMemory(ShownPage,Memory);
}
void UAetherFrontierPanel::BindCharacter()
{
 auto* C=Cast<AAetherFrontierCharacter>(GetOwningPlayerPawn());
 if(BoundCharacter.Get()==C)return;
 if(BoundCharacter.IsValid())BoundCharacter->OnPresentationChanged.RemoveAll(this);
 BoundCharacter=C;ShownRevision=-1;ShownShop=NAME_None;ShownTradeToken.Invalidate();
 if(C)C->OnPresentationChanged.AddUObject(this,&UAetherFrontierPanel::RefreshSnapshot);
 BindProfile();
}
void UAetherFrontierPanel::BindProfile()
{
 auto* PS=BoundCharacter.IsValid()?BoundCharacter->ProfileState():nullptr;
 if(BoundProfile.Get()==PS)return;
 if(BoundProfile.IsValid())BoundProfile->OnProfilePublished.RemoveAll(this);
 BoundProfile=PS;ShownRevision=-1;
 if(PS)PS->OnProfilePublished.AddUObject(this,&UAetherFrontierPanel::RefreshSnapshot);
}
void UAetherFrontierPanel::HandleMenuChanged()
{
 SavePageMemory();BindCharacter();
 auto* C=BoundCharacter.Get();ShownPage=C&&C->bPanel?EAetherMenuPage(C->Panel):EAetherMenuPage::None;
 RefreshSnapshot();
 if(Menu.IsValid()&&BodyScroll&&ShownPage!=EAetherMenuPage::None)
 {
  const auto Memory=Menu->GetPageMemory(ShownPage);BodyScroll->SetScrollOffset(Memory.ScrollOffset);
 }
 GetWorld()->GetTimerManager().ClearTimer(LiveDetailsTimer);
 if(C&&C->bPanel)GetWorld()->GetTimerManager().SetTimer(LiveDetailsTimer,this,&UAetherFrontierPanel::RefreshLiveDetails,.25f,true);
}
void UAetherFrontierPanel::RefreshLiveDetails()
{
 auto* C=BoundCharacter.Get();if(!C||!C->bPanel)return;
 RefreshTrade(C,C->Panel==1);
 // 队友生命和交易距离不是档案版本；只在相关页面打开时更新实时展示。
 if(C->Panel==5&&Model){Model->Refresh(C);Body->SetText(Model->Body);}
}
TSharedRef<SWidget> UAetherFrontierPanel::RebuildWidget()
{
 if(!WidgetTree)WidgetTree=NewObject<UWidgetTree>(this,TEXT("WidgetTree"));
 if(WidgetTree->RootWidget)return Super::RebuildWidget();
 Model=NewObject<UAetherFrontierViewModel>(this);
 auto* Border=WidgetTree->ConstructWidget<UBorder>();Border->SetBrushColor(FLinearColor(.015f,.025f,.04f,.98f));Border->SetPadding(FMargin(24));WidgetTree->RootWidget=Border;
 auto* Box=WidgetTree->ConstructWidget<UVerticalBox>();Border->SetContent(Box);
 Heading=WidgetTree->ConstructWidget<UTextBlock>();Heading->SetColorAndOpacity(FSlateColor(FLinearColor(1,.8f,.4f)));Box->AddChildToVerticalBox(Heading);
 auto* Scroll=WidgetTree->ConstructWidget<UScrollBox>();BodyScroll=Scroll;auto* BodySlot=Box->AddChildToVerticalBox(Scroll);BodySlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));BodySlot->SetPadding(FMargin(0,18));
 Body=WidgetTree->ConstructWidget<UTextBlock>();Body->SetAutoWrapText(true);Scroll->AddChild(Body);
 auto* Row=WidgetTree->ConstructWidget<UHorizontalBox>();Box->AddChildToVerticalBox(Row);
 auto Add=[&](const TCHAR* Label,UTextBlock*& Text){auto* Button=WidgetTree->ConstructWidget<UButton>();Text=WidgetTree->ConstructWidget<UTextBlock>();Text->SetText(FText::FromString(Label));Button->SetContent(Text);Row->AddChildToHorizontalBox(Button);return Button;};
 InventoryRow=WidgetTree->ConstructWidget<UHorizontalBox>();Box->AddChildToVerticalBox(InventoryRow);
 Quantity=WidgetTree->ConstructWidget<USpinBox>();Quantity->SetMinValue(1);Quantity->SetMaxValue(1000);Quantity->SetDelta(1);Quantity->SetMinDesiredWidth(80);Quantity->SetMinFractionalDigits(0);Quantity->SetMaxFractionalDigits(0);Quantity->SetValue(1);Quantity->OnValueChanged.AddDynamic(this,&UAetherFrontierPanel::SetQuantity);InventoryRow->AddChildToHorizontalBox(Quantity);
 MergeTarget=WidgetTree->ConstructWidget<UComboBoxString>();InventoryRow->AddChildToHorizontalBox(MergeTarget);MergeTarget->OnSelectionChanged.AddDynamic(this,&UAetherFrontierPanel::SetMergeTarget);
 TradeRow=WidgetTree->ConstructWidget<UHorizontalBox>();Box->AddChildToVerticalBox(TradeRow);
 Product=WidgetTree->ConstructWidget<UComboBoxString>();TradeRow->AddChildToHorizontalBox(Product);
 auto InventoryButton=[&](const TCHAR* Label){auto* B=WidgetTree->ConstructWidget<UButton>();auto* T=WidgetTree->ConstructWidget<UTextBlock>();T->SetText(FText::FromString(Label));B->SetContent(T);InventoryRow->AddChildToHorizontalBox(B);return B;};
 InventoryButton(TEXT("拆分"))->OnClicked.AddDynamic(this,&UAetherFrontierPanel::Split);
 InventoryButton(TEXT("合并至"))->OnClicked.AddDynamic(this,&UAetherFrontierPanel::Merge);
 RetryButton=InventoryButton(TEXT("重试上次操作"));RetryButton->OnClicked.AddDynamic(this,&UAetherFrontierPanel::RetryPending);
 auto TradeButton=[&](const TCHAR* Label,UTextBlock*& Text){auto* B=WidgetTree->ConstructWidget<UButton>();Text=WidgetTree->ConstructWidget<UTextBlock>();Text->SetText(FText::FromString(Label));B->SetContent(Text);TradeRow->AddChildToHorizontalBox(B);return B;};
 UTextBlock* BuyText=nullptr;UTextBlock* SellLabel=nullptr;
 BuyButton=TradeButton(TEXT("购买"),BuyText);BuyButton->OnClicked.AddDynamic(this,&UAetherFrontierPanel::Buy);
 SellButton=TradeButton(TEXT("出售"),SellLabel);SellText=SellLabel;SellButton->OnClicked.AddDynamic(this,&UAetherFrontierPanel::Sell);
 TradeInfo=WidgetTree->ConstructWidget<UTextBlock>();TradeInfo->SetAutoWrapText(true);Box->AddChildToVerticalBox(TradeInfo);
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
void UAetherFrontierPanel::RefreshSnapshot()
{
 BindProfile();auto* C=BoundCharacter.Get();if(!C){SetVisibility(ESlateVisibility::Collapsed);return;}
 // 关闭后真正折叠。后续由菜单/复制事件唤醒，不保留透明的逐帧轮询控件。
 const bool Open=C->bPanel&&C->Panel!=4;SetVisibility(Open?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
 if(!Open)return;
 InventoryRow->SetVisibility(Open&&C->Panel==1?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
 if(Open&&C->Panel==1&&FMath::RoundToInt(Quantity->GetValue())!=C->InventoryQuantity)Quantity->SetValue(C->InventoryQuantity);
 if(Open&&C->Panel==1&&C->ProfileState()&&ShownRevision!=C->ProfileState()->Profile.Revision)
 {
  ShownRevision=C->ProfileState()->Profile.Revision;const auto Selected=C->MergeDestination;MergeTarget->ClearOptions();MergeInstances.Reset();
  for(const auto& I:C->ProfileState()->Profile.Inventory){MergeInstances.Add(I.InstanceId);MergeTarget->AddOption(FString::Printf(TEXT("%02d %s × %d"),MergeInstances.Num(),*I.DefinitionId.ToString(),I.Count));}
  if(MergeInstances.Contains(Selected))MergeTarget->SetSelectedIndex(MergeInstances.Find(Selected));

 }
 // 失败/丢回执保留原命令；即使交易会话关闭，也要有明确入口查询或重试它。
 RetryButton->SetVisibility(C->PendingInventory.CommandId.IsValid()?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
 RefreshTrade(C,Open&&C->Panel==1);
 SettingsRow->SetVisibility(Open&&C->Panel==6?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
 if(Open&&C->Panel==6&&BindingAction->GetOptionCount()==0)
 {TArray<FName> Names;C->InputActions.GetKeys(Names);Names.Sort(FNameLexicalLess());for(auto Name:Names)if(Name!="LookX"&&Name!="LookY"&&Name!="Escape")BindingAction->AddOption(Name.ToString());BindingAction->SetSelectedOption(TEXT("Cast"));}
 if(Open){Model->Refresh(C);Heading->SetText(Model->Heading);Body->SetText(Model->Body);PrimaryText->SetText(Model->PrimaryLabel);SecondaryText->SetText(Model->SecondaryLabel);SecondaryText->GetParent()->SetVisibility(Model->SecondaryLabel.ToString()==TEXT("Close")?ESlateVisibility::Collapsed:ESlateVisibility::Visible);}
}
void UAetherFrontierPanel::Primary()
{if(auto* C=Cast<AAetherFrontierCharacter>(GetOwningPlayerPawn())){if(C->Panel==1)C->SubmitInventory("Equip");else if(C->Panel==2)C->ServerAction("Claim");else if(C->Panel==5)C->ServerAction("Invite");else if(C->Panel==6)C->ServerAction("Save");else ClosePanel();}}
void UAetherFrontierPanel::Secondary()
{if(auto* C=Cast<AAetherFrontierCharacter>(GetOwningPlayerPawn())){if(C->Panel==1)C->SubmitInventory("Use");else if(C->Panel==5)C->ServerAction("AcceptInvite");else if(C->Panel==6)C->ServerAction("Recover");else ClosePanel();}}
void UAetherFrontierPanel::NextItem(){if(auto* C=Cast<AAetherFrontierCharacter>(GetOwningPlayerPawn()))C->CycleItem();}
void UAetherFrontierPanel::ClosePanel(){if(auto* C=Cast<AAetherFrontierCharacter>(GetOwningPlayerPawn()))C->ClosePanel();}

void UAetherFrontierPanel::ActionSelected(FString Action,ESelectInfo::Type)
{
 if(auto* C=Cast<AAetherFrontierCharacter>(GetOwningPlayerPawn())){UpdatingKey=true;BindingKey->SetSelectedKey(FInputChord(C->BindingFor(*Action)));UpdatingKey=false;}
}
void UAetherFrontierPanel::KeySelected(FInputChord Key)
{
 if(UpdatingKey)return;if(auto* C=Cast<AAetherFrontierCharacter>(GetOwningPlayerPawn()))C->AetherBind(*BindingAction->GetSelectedOption(),Key.Key);
 ActionSelected(BindingAction->GetSelectedOption(),ESelectInfo::Direct);
}

void UAetherFrontierPanel::SetQuantity(float V){if(auto* C=Cast<AAetherFrontierCharacter>(GetOwningPlayerPawn())){C->InventoryQuantity=FMath::Clamp(FMath::RoundToInt(V),1,1000);C->OnPresentationChanged.Broadcast();}}
void UAetherFrontierPanel::SetMergeTarget(FString V,ESelectInfo::Type){if(auto* C=Cast<AAetherFrontierCharacter>(GetOwningPlayerPawn())){const int32 Index=MergeTarget->FindOptionIndex(V);C->MergeDestination=MergeInstances.IsValidIndex(Index)?MergeInstances[Index]:FGuid();}}
void UAetherFrontierPanel::Split(){if(auto* C=Cast<AAetherFrontierCharacter>(GetOwningPlayerPawn()))C->SubmitInventory("Split");}
void UAetherFrontierPanel::Merge(){if(auto* C=Cast<AAetherFrontierCharacter>(GetOwningPlayerPawn()))C->SubmitInventory("Merge");}
void UAetherFrontierPanel::RetryPending(){if(auto* C=Cast<AAetherFrontierCharacter>(GetOwningPlayerPawn());C&&C->PendingInventory.CommandId.IsValid())C->SubmitInventory(NAME_None);}
void UAetherFrontierPanel::Buy(){if(auto* C=Cast<AAetherFrontierCharacter>(GetOwningPlayerPawn()))C->SubmitInventory("Buy",*Product->GetSelectedOption());}
void UAetherFrontierPanel::Sell(){if(auto* C=Cast<AAetherFrontierCharacter>(GetOwningPlayerPawn())){C->RequestSale();RefreshSnapshot();}}

void UAetherFrontierPanel::RefreshTrade(AAetherFrontierCharacter* C,bool Open)
{
 const auto Shop=Open?C->ActiveShop():NAME_None;
 const bool Active=!Shop.IsNone();const auto Token=Active?C->TradeSession.Token:FGuid();
 TradeRow->SetVisibility(Open&&Active?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
 TradeInfo->SetVisibility(Open?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
 // 会话变化也刷新商品，不依赖背包版本变化；绝不拼接多个商人的清单。
 if(ShownShop!=Shop||ShownTradeToken!=Token)
 {
  ShownShop=Shop;ShownTradeToken=Token;Product->ClearOptions();
  if(const auto* Products=FAetherRules::Get().Shops.Find(Shop))for(const auto Id:*Products)Product->AddOption(Id.ToString());
  if(Product->GetOptionCount())Product->SetSelectedIndex(0);
 }
 if(!Open)return;
 if(!Active){TradeInfo->SetText(FText::FromString(TEXT("与商人交谈后可查看商品。离开范围、被遮挡或进入战斗时交易关闭。")));return;}
 const auto* PS=C->ProfileState();if(!PS)return;
 const auto& Rules=FAetherRules::Get();const FName ProductId=*Product->GetSelectedOption();const auto* Rule=Rules.Items.Find(ProductId);
 const int32 QuantityValue=C->InventoryQuantity;
 const int64 Cost=Rule?int64(Rule->Buy)*QuantityValue:0;
 const bool Pending=C->PendingInventory.CommandId.IsValid();
 BuyButton->SetIsEnabled(Rule&&Rule->Buy>0&&QuantityValue>=1&&QuantityValue<=1000&&Cost<=PS->Profile.Gold&&!Pending);
 FString Details=Rule?FString::Printf(TEXT("当前商人商品：%s | 单价 %d | 数量 %d | 合价 %lld | 拥有 %d | 按需供应%s"),
  *ProductId.ToString(),Rule->Buy,QuantityValue,Cost,PS->Profile.Count(ProductId),Cost>PS->Profile.Gold?TEXT(" · 金币不足"):TEXT("")):TEXT("这位商人没有可购买的商品。");
 const auto* Item=PS->Profile.Inventory.FindByPredicate([&](const auto& I){return I.InstanceId==C->SelectedInstance;});
 const auto* ItemRule=Item?Rules.Items.Find(Item->DefinitionId):nullptr;
 FString SellReason;
 if(!Item||!ItemRule)SellReason=TEXT("请选择要出售的物品");
 else if(PS->Profile.Equipped.FindKey(Item->InstanceId))SellReason=TEXT("已装备，需先卸下");
 else if(!ItemRule->bRemovable||!ItemRule->bSellable||ItemRule->Sell<=0)SellReason=TEXT("该物品不可出售");
 else if(QuantityValue<1||QuantityValue>Item->Count)SellReason=TEXT("出售数量超过拥有数量");
 SellButton->SetIsEnabled(SellReason.IsEmpty()&&!Pending);
 if(SellReason.IsEmpty())Details+=FString::Printf(TEXT("\n选中物品卖价 %d，出售 %d 件可获得 %lld 金币。"),ItemRule->Sell,QuantityValue,int64(ItemRule->Sell)*QuantityValue);
 else Details+=TEXT("\n")+SellReason;
 const auto Confirmation=C->SaleConfirmationText();SellText->SetText(FText::FromString(Confirmation.IsEmpty()?TEXT("出售"):TEXT("确认出售")));
 if(!Confirmation.IsEmpty())Details+=TEXT("\n")+Confirmation+TEXT("。10 秒内再次确认；更换物品、数量或会话会取消。");
 if(Pending)Details+=TEXT("\n上次操作等待确认。");
 TradeInfo->SetText(FText::FromString(Details));
}

FReply UAetherFrontierPanel::NativeOnPreviewKeyDown(const FGeometry& Geometry,const FKeyEvent& Event)
{
 auto* C=BoundCharacter.Get();
 // 改键控件正在录入时由控件消费按键（含 Escape 取消），不能把 I/K 等当作切页。
 if(!C||!C->bPanel||(BindingKey&&BindingKey->GetIsSelectingKey()))return Super::NativeOnPreviewKeyDown(Geometry,Event);
 if(Event.GetKey()==EKeys::Escape){if(!Event.IsRepeat())C->MenuBack();return FReply::Handled();}
 const TPair<FName,int32> Pages[]={{"I",1},{"J",2},{"K",3},{"M",4},{"P",5}};
 for(const auto& Page:Pages)
  if(Event.GetKey()==C->BindingFor(Page.Key)){if(!Event.IsRepeat())C->SelectPanel(Page.Value);return FReply::Handled();}
 return Super::NativeOnPreviewKeyDown(Geometry,Event);
}

UWidget* UAetherFrontierPanel::GetPrimaryFocusTarget() const
{return PrimaryText?PrimaryText->GetParent():nullptr;}
