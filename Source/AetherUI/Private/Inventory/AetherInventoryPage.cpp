#include "Inventory/AetherInventoryPage.h"
#include "UI/AetherWidgetAssets.h"
#include "UI/AetherPageWidgets.h"
#include "UI/AetherMenuRoot.h"
#include "Inventory/AetherNativeInventory.h"
#include "Inventory/AetherItemEligibility.h"
#include "Inspection/AetherInspectionWidgets.h"
#include "Preview/AetherCharacterPreviewWidget.h"
#include "Networking/AetherCommandClient.h"
#include "Presentation/AetherMenuSubsystem.h"
#include "Characters/AetherFrontierCharacter.h"
#include "Definitions/AetherV10Definitions.h"
#include "Blueprint/WidgetTree.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Components/EditableTextBox.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/Button.h"
#include "Engine/LocalPlayer.h"
#include "TimerManager.h"
#include "InputCoreTypes.h"
namespace
{
FString SlotName(const FString& Id)
{
    static const TMap<FString,FString> Names={{TEXT("MainHand"),TEXT("主手")},{TEXT("OffHand"),TEXT("副手")},{TEXT("Head"),TEXT("头部")},{TEXT("Chest"),TEXT("胸部")},
        {TEXT("Hands"),TEXT("手部")},{TEXT("Legs"),TEXT("腿部")},{TEXT("Feet"),TEXT("足部")},{TEXT("Neck"),TEXT("项链")},{TEXT("Ring1"),TEXT("戒指一")},{TEXT("Ring2"),TEXT("戒指二")}};
    if(const auto* N=Names.Find(Id))return *N;return Id;
}
FString ResultText(EAetherCommandCode Code)
{
    using E=EAetherCommandCode;switch(Code)
    {
    case E::Applied:case E::Replayed:return TEXT("操作已确认，背包以服务器快照为准。");
    case E::StaleRevision:case E::Conflict:return TEXT("对象已变化，请重新选择。");
    case E::Capacity:return TEXT("容量不足，请整理空格。");
    case E::InsufficientFunds:return TEXT("金币不足。");
    case E::OutOfReach:return TEXT("交易目标已失效，请重新靠近商人。");
    case E::Busy:case E::StorageUnavailable:return TEXT("原请求仍待确认；可以重试同一请求。");
    case E::NotReady:return TEXT("当前动作、战斗或冷却条件不允许。");
    default:return TEXT("操作未完成，请检查物品限制和当前条件。");
    }
}
}
TSharedRef<SWidget> UAetherInventoryPage::RebuildWidget()
{
    SetIsFocusable(true);if(!WidgetTree)WidgetTree=NewObject<UWidgetTree>(this);
    if(WidgetTree->RootWidget)AetherWidgetAssets::BindDesigner(*this,*WidgetTree);
    if(!WidgetTree->RootWidget)
    {
        auto* Overlay=WidgetTree->ConstructWidget<UOverlay>();WidgetTree->RootWidget=Overlay;
        auto* Root=WidgetTree->ConstructWidget<UVerticalBox>();Overlay->AddChildToOverlay(Root);
        Summary=WidgetTree->ConstructWidget<UTextBlock>();Root->AddChildToVerticalBox(Summary);
        auto* Toolbar=WidgetTree->ConstructWidget<UHorizontalBox>();Root->AddChildToVerticalBox(Toolbar)->SetPadding(FMargin(0,8));
        Search=WidgetTree->ConstructWidget<UEditableTextBox>();Search->SetHintText(FText::FromString(TEXT("搜索名称或定义")));Toolbar->AddChildToHorizontalBox(Search)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        Search->OnTextChanged.AddDynamic(this,&UAetherInventoryPage::SearchChanged);
        Categories=WidgetTree->ConstructWidget<UComboBoxString>();Toolbar->AddChildToHorizontalBox(Categories);Categories->OnSelectionChanged.AddDynamic(this,&UAetherInventoryPage::CategoryChanged);
        auto Button=[&](const TCHAR* Text){auto* B=WidgetTree->ConstructWidget<UButton>();auto* T=WidgetTree->ConstructWidget<UTextBlock>();T->SetText(FText::FromString(Text));B->SetContent(T);Toolbar->AddChildToHorizontalBox(B);return B;};
        Button(TEXT("整理并合并"))->OnClicked.AddDynamic(this,&UAetherInventoryPage::Sort);
        Button(TEXT("同步 / 重试"))->OnClicked.AddDynamic(this,&UAetherInventoryPage::Retry);
        StatusBar=WidgetTree->ConstructWidget<UHorizontalBox>();Root->AddChildToVerticalBox(StatusBar);
        Notice=WidgetTree->ConstructWidget<UTextBlock>();Notice->SetAutoWrapText(true);Root->AddChildToVerticalBox(Notice);
        auto* Body=WidgetTree->ConstructWidget<UHorizontalBox>();Root->AddChildToVerticalBox(Body)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        auto Column=[&](float Weight){auto* Scroll=WidgetTree->ConstructWidget<UScrollBox>();auto* SlotValue=Body->AddChildToHorizontalBox(Scroll);FSlateChildSize Size(ESlateSizeRule::Fill);Size.Value=Weight;SlotValue->SetSize(Size);SlotValue->SetPadding(FMargin(6,0));auto* Box=WidgetTree->ConstructWidget<UVerticalBox>();Scroll->AddChild(Box);return Box;};
        auto* Left=Column(.25f);auto* Center=Column(.5f);auto* Right=Column(.25f);
        auto* PreviewSize=WidgetTree->ConstructWidget<USizeBox>();PreviewSize->SetHeightOverride(300);Left->AddChildToVerticalBox(PreviewSize);
        Preview=CreateWidget<UAetherCharacterPreviewWidget>(this,AetherWidgetAssets::Class<UAetherCharacterPreviewWidget>());PreviewSize->SetContent(Preview);
        Equipment=WidgetTree->ConstructWidget<UUniformGridPanel>();Equipment->SetSlotPadding(FMargin(3));Left->AddChildToVerticalBox(Equipment);
        Grid=WidgetTree->ConstructWidget<UUniformGridPanel>();Grid->SetSlotPadding(FMargin(3));Center->AddChildToVerticalBox(Grid);
        Products=WidgetTree->ConstructWidget<UUniformGridPanel>();Products->SetSlotPadding(FMargin(3));Center->AddChildToVerticalBox(Products)->SetPadding(FMargin(0,16));
        ContainerTitle=WidgetTree->ConstructWidget<UTextBlock>();Center->AddChildToVerticalBox(ContainerTitle);
        ContainerGrid=WidgetTree->ConstructWidget<UUniformGridPanel>();ContainerGrid->SetSlotPadding(FMargin(3));Center->AddChildToVerticalBox(ContainerGrid)->SetPadding(FMargin(0,12));
        Details=CreateWidget<UAetherInspectionCard>(this,AetherWidgetAssets::Class<UAetherInspectionCard>());Right->AddChildToVerticalBox(Details);
        Details->OnActionRequested.AddUObject(this,&UAetherInventoryPage::Action);Details->OnComparisonRequested.AddUObject(this,&UAetherInventoryPage::Compare);Details->OnDismissRequested.AddUObject(this,&UAetherInventoryPage::DismissDetails);
        Hover=CreateWidget<UAetherInspectionCard>(this,AetherWidgetAssets::Class<UAetherInspectionCard>());Right->AddChildToVerticalBox(Hover);Hover->SetVisibility(ESlateVisibility::Collapsed);
        Confirmation=CreateWidget<UAetherInspectionConfirmation>(this,AetherWidgetAssets::Class<UAetherInspectionConfirmation>());
    }

    if(!Confirmation)Confirmation=CreateWidget<UAetherInspectionConfirmation>(this,AetherWidgetAssets::Class<UAetherInspectionConfirmation>());
    if(Search)Search->OnTextChanged.AddUniqueDynamic(this,&UAetherInventoryPage::SearchChanged);
    if(Categories)Categories->OnSelectionChanged.AddUniqueDynamic(this,&UAetherInventoryPage::CategoryChanged);
    AetherWidgetAssets::BindButton(*this,TEXT("SortButton"),TEXT("Sort"));AetherWidgetAssets::BindButton(*this,TEXT("RetryButton"),TEXT("Retry"));
    if(Hover)Hover->SetVisibility(ESlateVisibility::Collapsed);
    return Super::RebuildWidget();
}
void UAetherInventoryPage::NativeConstruct()
{
    if(Details){Details->OnActionRequested.RemoveAll(this);Details->OnComparisonRequested.RemoveAll(this);Details->OnDismissRequested.RemoveAll(this);
        Details->OnActionRequested.AddUObject(this,&UAetherInventoryPage::Action);Details->OnComparisonRequested.AddUObject(this,&UAetherInventoryPage::Compare);Details->OnDismissRequested.AddUObject(this,&UAetherInventoryPage::DismissDetails);}
    Super::NativeConstruct();auto* LP=GetOwningLocalPlayer();if(!LP)return;
    Menu=LP->GetSubsystem<UAetherMenuSubsystem>();Client=LP->GetSubsystem<UAetherCommandClient>();
    Menu->OnChanged.AddUObject(this,&UAetherInventoryPage::MenuChanged);
    Client->OnChanged.AddUObject(this,&UAetherInventoryPage::Refresh);Client->OnResult.AddUObject(this,&UAetherInventoryPage::Receive);
    bUpdating=true;Categories->ClearOptions();Categories->AddOption(TEXT("全部"));
    TArray<FString> Names;for(const auto& Pair:FAetherV10Definitions::Get().Items.Items)Names.AddUnique(Pair.Value.Category);Names.Sort();
    for(const auto& N:Names)Categories->AddOption(N);Categories->SetSelectedOption(TEXT("全部"));bUpdating=false;MenuChanged();
}
void UAetherInventoryPage::NativeDestruct()
{
    if(Menu.IsValid())Menu->OnChanged.RemoveAll(this);
    if(Client.IsValid()){Client->OnChanged.RemoveAll(this);Client->OnResult.RemoveAll(this);}
    ClosePresentation();Menu.Reset();Client.Reset();Super::NativeDestruct();
}
void UAetherInventoryPage::MenuChanged()
{
    const bool Open=Menu.IsValid()&&Menu->GetPage()==EAetherMenuPage::Inventory;
    if(!Open){ClosePresentation();return;}
    if(ModalToken.IsValid()&&!Menu->HasLayer(ModalToken)){if(Session.GetDraft().IsSet())Session.Back();HideConfirmation();}
    if(!bWasOpen)
    {
        bWasOpen=true;bDirty=true;const auto Memory=Menu->GetPageMemory(EAetherMenuPage::Inventory);SearchFilter=Memory.Search;
        bUpdating=true;Search->SetText(FText::FromString(SearchFilter));bUpdating=false;
        GetWorld()->GetTimerManager().SetTimer(ServiceTimer,this,&UAetherInventoryPage::Refresh,.25f,true);
    }
    Refresh();
    if(Menu.IsValid()&&Menu->RequestedItem().IsValid()&&Snapshot.Context.IsValid())
    {
        const FGuid Id=Menu->RequestedItem();Menu->ConsumeInspection(Id);FAetherInspectTarget Target;Target.InstanceId=Id;
        Session.OpenDetails(Target,Snapshot,FAetherV10Definitions::Get().Items,FAetherV10Definitions::Get().Skills);RenderDetails();
    }
}
void UAetherInventoryPage::ClosePresentation()
{
    bWasOpen=false;if(GetWorld())GetWorld()->GetTimerManager().ClearTimer(ServiceTimer);
    ResetPresentation();
}
void UAetherInventoryPage::ResetPresentation()
{
    HideConfirmation();Session=FAetherInspectionSession();Snapshot={};Player.Reset();
    ++Generation;SeenProfile=SeenWorld=SeenContainerRevision=SeenContainerWorld=-1;
    SeenChannel.Invalidate();SeenTrade.Invalidate();SeenContainerContext.Invalidate();SeenSelected.Invalidate();
    SeenShop.Reset();SeenStatuses.Reset();bSeenCanAct=false;bDirty=true;
    if(Grid)Grid->ClearChildren();if(Equipment)Equipment->ClearChildren();
    if(Products)Products->ClearChildren();if(ContainerGrid)ContainerGrid->ClearChildren();
    if(StatusBar)StatusBar->ClearChildren();
    Cells.Reset();EquipmentCells.Reset();ProductCells.Reset();ContainerCells.Reset();
    if(ContainerTitle)ContainerTitle->SetText(FText::GetEmpty());if(Notice)Notice->SetText(FText::GetEmpty());
    if(Preview)Preview->SetSource(nullptr);
    if(Details)Details->SetModel(FAetherInspectionModel());
    if(Hover){Hover->SetModel(FAetherInspectionModel());Hover->SetVisibility(ESlateVisibility::Collapsed);}
}
void UAetherInventoryPage::Refresh()
{
    if(bUpdating||!bWasOpen||!Client.IsValid()||!Grid)return;TGuardValue<bool> Guard(bUpdating,true);
    auto* C=Cast<AAetherFrontierCharacter>(GetOwningPlayerPawn());FAetherInspectionSnapshot Next;
    if(Client->HasPending()&&Notice)Notice->SetText(FText::FromString(Client->PendingDescription()));
    if(!C||!AetherNativeInventory::Snapshot(*C,Generation,Next))
    {ResetPresentation();Summary->SetText(FText::FromString(TEXT("等待原生背包同步")));return;}
    if(Player.Get()!=C||SeenChannel!=Next.Context.SessionId){ResetPresentation();Player=C;}
    // 列数取实际中心滚动视口；固定物理槽索引保持不变，重排不会改变命令目标。
    float Width=0;
    for(UWidget* Parent=Grid->GetParent();Parent;Parent=Parent->GetParent())
        if(Cast<UScrollBox>(Parent)){Width=Parent->GetCachedGeometry().GetLocalSize().X;break;}
    if(Width>0)
    {
        const int32 NextColumns=FMath::Clamp(FMath::FloorToInt((Width-18)/82),2,8);
        if(NextColumns!=Columns){Columns=NextColumns;bDirty=true;}
    }
    const int64 BeforeGeneration=Generation;
    const FString CurrentShop=Next.Shop.IsSet()?Next.Shop->Id:FString();
    if(SeenSelected!=C->SelectedInstance){SeenSelected=C->SelectedInstance;bDirty=true;}
    if(SeenProfile!=Next.ProfileRevision||SeenWorld!=Next.WorldRevision||SeenTrade!=C->TradeSession.Token||bSeenCanAct!=Next.bCanAct||SeenShop!=CurrentShop)
    {
        ++Generation;SeenProfile=Next.ProfileRevision;SeenWorld=Next.WorldRevision;SeenChannel=Next.Context.SessionId;SeenTrade=C->TradeSession.Token;bSeenCanAct=Next.bCanAct;SeenShop=CurrentShop;
    }
    const int64 ContainerRevision=Next.Container.IsSet()?Next.Container->Revision:-1;
    if(SeenContainerContext!=Next.ContainerContext||SeenContainerRevision!=ContainerRevision||SeenContainerWorld!=Next.ContainerWorldRevision)
    {++Generation;SeenContainerContext=Next.ContainerContext;SeenContainerRevision=ContainerRevision;SeenContainerWorld=Next.ContainerWorldRevision;}
    FString StatusKey;
    for(const auto& Effect:Next.StatusEffects)StatusKey+=Effect.InstanceId.ToString()+FString::SanitizeFloat(Effect.ExpiresAtServerSeconds.Get(0));
    if(StatusKey!=SeenStatuses){SeenStatuses=StatusKey;++Generation;}
    if(BeforeGeneration==Generation&&!bDirty)
    {
        // 时间变化不改变对象身份；详情仍用同一实例更新倒计时，不强制重建格网/焦点。
        Snapshot.ServerTimeSeconds=Next.ServerTimeSeconds;Snapshot.StatusEffects=MoveTemp(Next.StatusEffects);Snapshot.UseSummary=Next.UseSummary;Snapshot.bCanAct=Next.bCanAct;
        if(Session.GetDetails().IsSet())
        {const auto& D=FAetherV10Definitions::Get();Session.Refresh(Snapshot,D.Items,D.Skills);RenderDetails();}
        return;
    }
    bDirty=false;Next.Context.SnapshotRevision=Generation;Snapshot=MoveTemp(Next);const auto& D=FAetherV10Definitions::Get();
    Session.Refresh(Snapshot,D.Items,D.Skills);if(!Session.GetDraft().IsSet())HideConfirmation();
    Preview->SetSource(C);
    StatusBar->ClearChildren();
    for(const auto& Effect:Snapshot.StatusEffects)
    {
        auto* B=WidgetTree->ConstructWidget<UAetherPageButton>();auto* Text=WidgetTree->ConstructWidget<UTextBlock>();
        Text->SetText(FText::FromString(Effect.DisplayName+TEXT(" · 详情")));B->SetContent(Text);StatusBar->AddChildToHorizontalBox(B);
        B->Bind(FSimpleDelegate::CreateWeakLambda(this,[this,Id=Effect.InstanceId]()
        {
            FAetherInspectTarget Target;Target.Kind=EAetherInspectTarget::StatusEffect;Target.InstanceId=Id;
            const auto& D=FAetherV10Definitions::Get();Session.OpenDetails(Target,Snapshot,D.Items,D.Skills);RenderDetails();Details->SetUserFocus(GetOwningPlayer());
        }));
    }
    Summary->SetText(FText::FromString(FString::Printf(TEXT("背包 %d / %d · 金币 %d%s"),Snapshot.Inventory.Items.Num(),Snapshot.Inventory.Capacity,Snapshot.Gold,Snapshot.Shop.IsSet()?TEXT(" · 商店服务已开启"):TEXT(""))));
    const auto MakeCell=[&](UUniformGridPanel* Parent,int32 Index,int32 ColumnCount)
    {
        auto* Cell=CreateWidget<UAetherInventoryCell>(this,AetherWidgetAssets::Class<UAetherInventoryCell>(Parent==Equipment?TEXT("WBP_EquipmentSlot"):nullptr));auto* CellSlot=Parent->AddChildToUniformGrid(Cell,Index/ColumnCount,Index%ColumnCount);
        CellSlot->SetHorizontalAlignment(HAlign_Fill);CellSlot->SetVerticalAlignment(VAlign_Fill);
        Cell->OnIntent.BindUObject(this,&UAetherInventoryPage::CellIntent);Cell->OnItemDrop.BindUObject(this,&UAetherInventoryPage::Drop);Cell->OnDropPreview.BindUObject(this,&UAetherInventoryPage::PreviewDrop);return Cell;
    };
    if(Cells.Num()!=Snapshot.Inventory.Capacity)
    {Grid->ClearChildren();Cells.Reset();for(int32 I=0;I<Snapshot.Inventory.Capacity;++I)Cells.Add(MakeCell(Grid,I,Columns));}
    for(int32 SlotValue=0;SlotValue<Cells.Num();++SlotValue)
    {
        if(auto* Layout=Cast<UUniformGridSlot>(Cells[SlotValue]->Slot)){Layout->SetRow(SlotValue/Columns);Layout->SetColumn(SlotValue%Columns);}
        const auto* I=Snapshot.Inventory.At(SlotValue);const auto* Def=I?D.Items.Items.Find(I->DefinitionId):nullptr;
        FAetherInspectTarget Target;Target.Kind=EAetherInspectTarget::ItemInstance;if(I)Target.InstanceId=I->InstanceId;
        FString Label=FString::Printf(TEXT("%02d · 空"),SlotValue+1);bool Filtered=false;
        if(I&&Def)
        {
            Label=Def->DisplayName;
            Filtered=(!CategoryFilter.IsEmpty()&&Def->Category!=CategoryFilter)||(!SearchFilter.IsEmpty()&&!Def->DisplayName.Contains(SearchFilter)&&!Def->Id.Contains(SearchFilter));
        }
        Cells[SlotValue]->Present(AetherInspection::Pin(Snapshot,Target),SlotValue,Label,Def?Def->IconId:FString(),Filtered,I&&I->InstanceId==C->SelectedInstance);
        Cells[SlotValue]->SetItemState(I,Def,I&&Snapshot.Inventory.IsEquipped(I->InstanceId));
        // Let spatial navigation cross region boundaries; never wrap back into the same grid.
        Cells[SlotValue]->SetNavigationRuleBase(EUINavigation::Left,EUINavigationRule::Escape);
        Cells[SlotValue]->SetNavigationRuleBase(EUINavigation::Right,EUINavigationRule::Escape);
        Cells[SlotValue]->SetNavigationRuleBase(EUINavigation::Up,EUINavigationRule::Escape);
        Cells[SlotValue]->SetNavigationRuleBase(EUINavigation::Down,EUINavigationRule::Escape);
    }
    if(EquipmentCells.Num()!=D.Items.Slots.Num())
    {Equipment->ClearChildren();EquipmentCells.Reset();for(int32 I=0;I<D.Items.Slots.Num();++I)EquipmentCells.Add(MakeCell(Equipment,I,2));}
    for(int32 N=0;N<D.Items.Slots.Num();++N)
    {
        const auto& SlotValue=D.Items.Slots[N];FAetherInspectTarget T;T.Kind=EAetherInspectTarget::EquipmentSlot;T.SlotId=SlotValue.Id;
        const auto R=AetherInspection::Pin(Snapshot,T);const auto* I=Snapshot.Inventory.Find(R.Target.InstanceId);const auto* Def=I?D.Items.Items.Find(I->DefinitionId):nullptr;
        FString Occupied;for(const auto& Pair:Snapshot.Inventory.Equipment)
            if(const auto* Other=Snapshot.Inventory.Find(Pair.Value))if(const auto* OtherDef=D.Items.Items.Find(Other->DefinitionId);OtherDef&&OtherDef->AdditionalOccupiedSlots.Contains(SlotValue.Id))Occupied=TEXT("双手占用");
        EquipmentCells[N]->Present(R,INDEX_NONE,SlotName(SlotValue.Id)+LINE_TERMINATOR+(Def?Def->DisplayName:Occupied.IsEmpty()?TEXT("空"):Occupied),Def?Def->IconId:FString(),false,false);
        EquipmentCells[N]->SetItemState(I,Def,I!=nullptr);
    }
    const auto* Container=Snapshot.Container.IsSet()?&Snapshot.Container.GetValue():nullptr;
    ContainerTitle->SetText(FText::FromString(Container?FString::Printf(TEXT("%s · %d / %d"),Container->Kind==EAetherContainerKind::WorldDrop?TEXT("地面掉落"):Container->Kind==EAetherContainerKind::PersonalStorage?TEXT("个人仓储"):TEXT("共享箱子"),Container->Inventory.Items.Num(),Container->Inventory.Capacity):
        Snapshot.ContainerContext.IsValid()?TEXT("正在读取容器…"):TEXT("")));
    ContainerGrid->SetVisibility(Container?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
    const int32 Count=Container?Container->Inventory.Capacity:0;
    if(ContainerCells.Num()!=Count){ContainerGrid->ClearChildren();ContainerCells.Reset();for(int32 N=0;N<Count;++N)ContainerCells.Add(MakeCell(ContainerGrid,N,Columns));}
    for(int32 N=0;N<Count;++N)
    {
        if(auto* Layout=Cast<UUniformGridSlot>(ContainerCells[N]->Slot)){Layout->SetRow(N/Columns);Layout->SetColumn(N%Columns);}
        const auto* Item=Container->Inventory.At(N);const auto* Def=Item?D.Items.Items.Find(Item->DefinitionId):nullptr;
        FAetherInspectTarget T;T.ContainerId=Container->ContainerId;if(Item)T.InstanceId=Item->InstanceId;
        ContainerCells[N]->Present(AetherInspection::Pin(Snapshot,T),N,Def?Def->DisplayName:TEXT("空"),Def?Def->IconId:FString(),false,false);
        ContainerCells[N]->SetItemState(Item,Def);
        ContainerCells[N]->SetNavigationRuleBase(EUINavigation::Left,EUINavigationRule::Escape);
        ContainerCells[N]->SetNavigationRuleBase(EUINavigation::Right,EUINavigationRule::Escape);
        ContainerCells[N]->SetNavigationRuleBase(EUINavigation::Up,EUINavigationRule::Escape);
        ContainerCells[N]->SetNavigationRuleBase(EUINavigation::Down,EUINavigationRule::Escape);
    }
    const auto* Shop=Snapshot.Shop.IsSet()?&Snapshot.Shop.GetValue():nullptr;
    Products->SetVisibility(Shop?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
    const int32 ProductCount=Shop?Shop->Products.Num():0;
    if(ProductCells.Num()!=ProductCount){Products->ClearChildren();ProductCells.Reset();for(int32 I=0;I<ProductCount;++I)ProductCells.Add(MakeCell(Products,I,4));}
    for(int32 N=0;N<ProductCount;++N)
    {
        if(auto* Layout=Cast<UUniformGridSlot>(ProductCells[N]->Slot)){const int32 ProductColumns=FMath::Min(Columns,4);Layout->SetRow(N/ProductColumns);Layout->SetColumn(N%ProductColumns);}
        const auto* Def=D.Items.Items.Find(Shop->Products[N]);if(!Def)continue;
        FAetherInspectTarget T;T.Kind=EAetherInspectTarget::ItemDefinition;T.DefinitionId=Def->Id;
        ProductCells[N]->Present(AetherInspection::Pin(Snapshot,T),INDEX_NONE,Def->DisplayName+LINE_TERMINATOR+FString::Printf(TEXT("%d 金币"),Def->BuyPrice),Def->IconId,false,false);
    }
    RenderDetails();
}
void UAetherInventoryPage::CellIntent(const FAetherInspectRequest& R,int32,EAetherCellIntent Intent)
{
    if(!R.Context.Same(Snapshot.Context)||ModalToken.IsValid())return;const auto& D=FAetherV10Definitions::Get();
    if(Intent==EAetherCellIntent::Leave){Session.HideHover();RenderDetails();return;}
    if(R.Target.Kind==EAetherInspectTarget::ItemInstance&&!R.Target.InstanceId.IsValid())return;
    if(Player.IsValid()&&R.Target.ContainerId.IsEmpty()&&R.Target.InstanceId.IsValid()&&Intent!=EAetherCellIntent::Hover)
    {
        Player->SelectedInstance=R.Target.InstanceId;bDirty=true;
        if(Menu.IsValid()){auto M=Menu->GetPageMemory(EAetherMenuPage::Inventory);M.SelectedInstance=R.Target.InstanceId;Menu->SavePageMemory(EAetherMenuPage::Inventory,M);}
    }
    if(Intent==EAetherCellIntent::Details){Session.OpenDetails(R.Target,Snapshot,D.Items,D.Skills);RenderDetails();Details->SetUserFocus(GetOwningPlayer());}
    else if(Intent==EAetherCellIntent::Hover){Session.ShowHover(R.Target,Snapshot,D.Items,D.Skills);RenderDetails();}
}
void UAetherInventoryPage::RenderDetails()
{
    if(!Details)return;
    if(Session.GetDetails().IsSet())Details->SetModel(Session.GetDetails().GetValue());
    else {FAetherInspectionModel M;M.Message=TEXT("右键或手柄确认查看物品；拖动格子移动、合并或交换。");Details->SetModel(M);}
    const bool Show=Session.GetHover().IsSet()&&!Session.GetDetails().IsSet();
    Hover->SetVisibility(Show?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);if(Show)Hover->SetModel(Session.GetHover().GetValue());
}
bool UAetherInventoryPage::PreviewDrop(const FAetherInspectRequest& From,const FAetherInspectRequest& To,int32 Slot,FString& Hint) const
{
    const auto Reject=[&](const TCHAR* Why){Hint=Why;return false;};
    if(!Snapshot.bCanAct||ModalToken.IsValid())return Reject(TEXT("当前动作或同步尚未完成"));
    if(!From.Context.Same(Snapshot.Context)||!To.Context.Same(Snapshot.Context)||
        From.DependencyKey!=AetherInspection::Pin(Snapshot,From.Target).DependencyKey||To.DependencyKey!=AetherInspection::Pin(Snapshot,To.Target).DependencyKey)return Reject(TEXT("对象已变化，请重新拖动"));
    const auto& D=FAetherV10Definitions::Get().Items;
    const bool Cross=!From.Target.ContainerId.IsEmpty()||!To.Target.ContainerId.IsEmpty();
    const auto* Source=From.Target.ContainerId.IsEmpty()?&Snapshot.Inventory:Snapshot.Container.IsSet()?&Snapshot.Container->Inventory:nullptr;
    const auto* Item=Source?Source->Find(From.Target.InstanceId):nullptr;if(!Item)return Reject(TEXT("原物品已不存在"));
    if(Cross)
    {
        if(!Snapshot.Container.IsSet()||From.Target.Kind!=EAetherInspectTarget::ItemInstance||To.Target.Kind!=EAetherInspectTarget::ItemInstance||From.Target.ContainerId==To.Target.ContainerId||
            (!From.Target.ContainerId.IsEmpty()&&!To.Target.ContainerId.IsEmpty()))return Reject(TEXT("此处不能跨容器放置"));
        const bool Into=From.Target.ContainerId.IsEmpty();const auto& Box=Snapshot.Container.GetValue();
        if((Into?To.Target.ContainerId:From.Target.ContainerId)!=Box.ContainerId)return Reject(TEXT("容器已切换"));
        if(Into&&Box.Kind==EAetherContainerKind::WorldDrop)return Reject(TEXT("战利品袋只允许取出"));
        const auto Operation=Box.Kind==EAetherContainerKind::PersonalStorage?EAetherItemOperation::PersonalStorage:EAetherItemOperation::SharedStorage;
        if(AetherItemEligibility::Query(*Source,Item->InstanceId,Snapshot.Context.OwnerIdentity,Into?Operation:EAetherItemOperation::Withdraw,D)!=EAetherInventoryMutationCode::Applied)return Reject(TEXT("该物品当前不能转移，请查看详情原因"));
        const auto& Destination=Into?Box.Inventory:Snapshot.Inventory;if(Slot<0||Slot>=Destination.Capacity)return Reject(TEXT("无效目标格"));
        const auto* Other=Destination.At(Slot);
        if(!Other){Hint=FString::Printf(TEXT("放入第 %d 格 · 确认数量"),Slot+1);return true;}
        if(Item->SameStackKey(*Other))
        {
            const auto* Def=D.Items.Find(Item->DefinitionId);if(!Def||Other->Quantity>=Def->MaxStack)return Reject(TEXT("目标堆已满"));
            Hint=FString::Printf(TEXT("合并到第 %d 格 · 最多 %d 件"),Slot+1,Def->MaxStack-Other->Quantity);return true;
        }
        if(Box.Kind==EAetherContainerKind::WorldDrop)return Reject(TEXT("战利品袋不能整堆交换"));
        if(Destination.IsEquipped(Other->InstanceId)||AetherItemEligibility::Query(Destination,Other->InstanceId,Snapshot.Context.OwnerIdentity,Into?EAetherItemOperation::Withdraw:Operation,D)!=EAetherInventoryMutationCode::Applied)return Reject(TEXT("目标物品不能反向交换"));
        Hint=TEXT("双方整堆交换 · 需确认");return true;
    }
    auto Candidate=Snapshot.Inventory;FAetherInventoryMutation Result;
    if(To.Target.Kind==EAetherInspectTarget::EquipmentSlot)
    {Result=Candidate.Equip(Item->InstanceId,To.Target.SlotId,Snapshot.Context.OwnerIdentity,D);Hint=TEXT("装备到指定部位");}
    else if(Slot<0||Slot>=Candidate.Capacity)return Reject(TEXT("无效目标格"));
    else if(From.Target.Kind==EAetherInspectTarget::EquipmentSlot)
    {Result=Candidate.UnequipTo(Item->InstanceId,Slot,D);Hint=TEXT("卸装并放入该格");}
    else if(const auto* Other=Candidate.At(Slot))
    {
        if(Item->InstanceId==Other->InstanceId)return Reject(TEXT("物品已在该格"));
        const auto* Def=D.Items.Find(Item->DefinitionId);
        if(Def&&Item->SameStackKey(*Other)&&Other->Quantity<Def->MaxStack)
        {Result=Candidate.Merge(Item->InstanceId,Other->InstanceId,FMath::Min(Item->Quantity,Def->MaxStack-Other->Quantity),D);Hint=TEXT("合并到该堆");}
        else {Result=Candidate.Swap(Item->InstanceId,Other->InstanceId,D);Hint=TEXT("交换背包格位置");}
    }
    else {Result=Candidate.Move(Item->InstanceId,Slot,D);Hint=TEXT("移动到该空格");}
    if(Result.Code!=EAetherInventoryMutationCode::Applied)return Reject(TEXT("此处不能放置，请查看装备或占格条件"));return true;
}
bool UAetherInventoryPage::Drop(const FAetherInspectRequest& From,const FAetherInspectRequest& To,int32 SlotValue)
{
    FString Hint;if(!PreviewDrop(From,To,SlotValue,Hint)){Notice->SetText(FText::FromString(Hint));return false;}
    if(!From.Context.Same(Snapshot.Context)||!To.Context.Same(Snapshot.Context)||!Snapshot.bCanAct||ModalToken.IsValid())return false;
    if(From.DependencyKey!=AetherInspection::Pin(Snapshot,From.Target).DependencyKey||To.DependencyKey!=AetherInspection::Pin(Snapshot,To.Target).DependencyKey)
    {Notice->SetText(FText::FromString(TEXT("拖动期间对象已变化，请重新选择。")));return false;}
    if(!From.Target.ContainerId.IsEmpty()||!To.Target.ContainerId.IsEmpty())
    {
        if(!Snapshot.Container.IsSet()||From.Target.Kind!=EAetherInspectTarget::ItemInstance||To.Target.Kind!=EAetherInspectTarget::ItemInstance||
            From.Target.ContainerId==To.Target.ContainerId||(!From.Target.ContainerId.IsEmpty()&&!To.Target.ContainerId.IsEmpty()))return false;
        const bool Into=From.Target.ContainerId.IsEmpty();
        const auto& Id=Into?To.Target.ContainerId:From.Target.ContainerId;
        if(!Id.Equals(Snapshot.Container->ContainerId,ESearchCase::CaseSensitive))return false;
        const auto& D=FAetherV10Definitions::Get();Session.OpenDetails(From.Target,Snapshot,D.Items,D.Skills);RenderDetails();
        const auto Kind=Into?EAetherInspectAction::Deposit:EAetherInspectAction::Withdraw;
        const auto* Model=Session.GetDetails().IsSet()?&Session.GetDetails().GetValue():nullptr;
        const auto* A=Model?Model->Actions.FindByPredicate([&](const auto& V){return V.Kind==Kind&&V.bEnabled;}):nullptr;
        if(!A||SlotValue<0)return false;
        const auto& SourceInventory=Into?Snapshot.Inventory:Snapshot.Container->Inventory;
        const auto& TargetInventory=Into?Snapshot.Container->Inventory:Snapshot.Inventory;
        const auto* SourceItem=SourceInventory.Find(From.Target.InstanceId);const auto* TargetItem=TargetInventory.At(SlotValue);
        if(!SourceItem)return false;
        const auto Mode=TargetItem&&!SourceItem->SameStackKey(*TargetItem)?EAetherTransferMode::SwapWhole:EAetherTransferMode::PlaceOrMerge;
        if(Mode==EAetherTransferMode::SwapWhole&&Snapshot.Container->Kind==EAetherContainerKind::WorldDrop)
        {Notice->SetText(FText::FromString(TEXT("战利品袋只能取出，不能交换放回。")));return false;}
        const auto Selected=*A;TransferAction(From,Selected,SlotValue,TargetItem?TargetItem->InstanceId:FGuid(),Mode);return true;
    }
    const auto* I=Snapshot.Inventory.Find(From.Target.InstanceId);if(!I)return false;FAetherPlayerCommand C;C.ItemInstanceId=I->InstanceId;
    if(To.Target.Kind==EAetherInspectTarget::EquipmentSlot){C.Type=EAetherCommandType::EquipItem;C.SlotId=To.Target.SlotId;}
    else if(To.Target.Kind!=EAetherInspectTarget::ItemInstance||SlotValue<0)return false;
    else if(From.Target.Kind==EAetherInspectTarget::EquipmentSlot)
    {
        if(const auto* Occupant=Snapshot.Inventory.At(SlotValue);Occupant&&Occupant->InstanceId!=I->InstanceId)
        {Notice->SetText(FText::FromString(TEXT("卸装落格需要空格或物品原格；可在详情中直接卸装。")));return false;}
        C.Type=EAetherCommandType::UnequipItem;C.DestinationIndex=SlotValue;
    }
    else if(const auto* Other=Snapshot.Inventory.At(SlotValue))
    {
        if(Other->InstanceId==I->InstanceId)return false;C.OtherInstanceId=Other->InstanceId;
        const auto* Def=FAetherV10Definitions::Get().Items.Items.Find(I->DefinitionId);
        if(Def&&I->SameStackKey(*Other)&&Other->Quantity<Def->MaxStack)
        {C.Type=EAetherCommandType::MergeStack;C.Quantity=FMath::Min(I->Quantity,Def->MaxStack-Other->Quantity);}
        else C.Type=EAetherCommandType::SwapItems;
    }
    else {C.Type=EAetherCommandType::MoveItem;C.DestinationIndex=SlotValue;}
    return Send(MoveTemp(C));
}
bool UAetherInventoryPage::Send(FAetherPlayerCommand C)
{
    FString Why;const bool Sent=Player.IsValid()&&AetherNativeInventory::Submit(*Player.Get(),MoveTemp(C),Snapshot.ProfileRevision,Why);
    Notice->SetText(FText::FromString(Why));return Sent;
}
void UAetherInventoryPage::Action(const FAetherInspectRequest& R,const FAetherInspectionAction& A)
{TransferAction(R,A,-1,{},EAetherTransferMode::QuickTransfer);}
void UAetherInventoryPage::TransferAction(const FAetherInspectRequest& R,const FAetherInspectionAction& A,int32 Destination,FGuid ExpectedTarget,EAetherTransferMode Mode)
{
    if(!R.Context.Same(Snapshot.Context)||!Client.IsValid()||Client->HasPending())return;
    FString DestinationLabel;
    if(ExpectedTarget.IsValid())
    {
        const auto& Inventory=A.Kind==EAetherInspectAction::Deposit?Snapshot.Container->Inventory:Snapshot.Inventory;
        if(const auto* Target=Inventory.Find(ExpectedTarget))if(const auto* Def=FAetherV10Definitions::Get().Items.Items.Find(Target->DefinitionId))DestinationLabel=Def->DisplayName;
    }
    const auto Token=Session.BeginAction(A.Kind,A.Argument,Destination,ExpectedTarget,Mode,DestinationLabel);if(!Token.IsValid())return;
    if(A.bNeedsConfirmation||A.MaxQuantity>1)
    {
        if(!Menu.IsValid())return;ModalToken=Menu->PushLayer(TEXT("InventoryConfirmation"));
        if(!ModalToken.IsValid()){Session.Back();return;}auto* Root=UAetherMenuRoot::Find(*this);
    if(!Root||!Root->PushModal(ModalToken,Confirmation)){Session.Back();HideConfirmation();return;}
    Confirmation->OnConfirmed.RemoveAll(this);Confirmation->OnCancelled.RemoveAll(this);
    Confirmation->OnConfirmed.AddUObject(this,&UAetherInventoryPage::Confirm);Confirmation->OnCancelled.AddUObject(this,&UAetherInventoryPage::Cancel);
    Confirmation->SetDraft(Session.GetDraft().GetValue());Confirmation->SetUserFocus(GetOwningPlayer());
    }
    else Confirm(Token,1);
}
void UAetherInventoryPage::Compare(const FAetherInspectRequest& R,const FString& SlotValue)
{
    if(!R.Context.Same(Snapshot.Context))return;auto T=R.Target;T.ComparisonSlot=SlotValue;
    Session.OpenDetails(T,Snapshot,FAetherV10Definitions::Get().Items,FAetherV10Definitions::Get().Skills);RenderDetails();
}
void UAetherInventoryPage::Confirm(FGuid Token,int32 Count)
{
    if(!Session.GetDraft().IsSet()||Session.GetDraft()->Token!=Token)return;
    // 先采集当前公开状态；交易关闭、受击或目标卸载会使旧确认失效。
    Refresh();if(!Session.GetDraft().IsSet()||Session.GetDraft()->Token!=Token)return;
    FAetherInspectionDispatch Dispatch;FString Why;const auto& D=FAetherV10Definitions::Get();
    const bool Built=Session.Confirm(Token,Count,Snapshot,D.Items,D.Skills,Dispatch,Why);HideConfirmation();
    if(Built&&Dispatch.Command.IsSet())
        if(!Client.IsValid()||!Client->Submit(Snapshot.Context.SessionId,Snapshot.Context.OwnerIdentity,Dispatch.CommandBytes,Why))
            Session.RejectBeforeSend(Dispatch.Command->CommandId);
    Notice->SetText(FText::FromString(Why));RenderDetails();
}
void UAetherInventoryPage::Cancel(FGuid Token)
{if(Session.GetDraft().IsSet()&&Session.GetDraft()->Token==Token){Session.Back();HideConfirmation();}}
void UAetherInventoryPage::HideConfirmation()
{
    const auto Token=ModalToken;ModalToken.Invalidate();if(Confirmation)Confirmation->InvalidateDraft();if(auto* Root=UAetherMenuRoot::Find(*this))Root->PopModal(Token);
    if(Menu.IsValid()&&Token.IsValid())Menu->DismissLayer(Token);
}
void UAetherInventoryPage::DismissDetails(){Session.Close();HideConfirmation();RenderDetails();}
void UAetherInventoryPage::Receive(const FAetherCommandResult& R)
{Session.Acknowledge(R);bDirty=true;Refresh();if(Notice)Notice->SetText(FText::FromString(R.ActualQuantity>0?FString::Printf(TEXT("已完成 %d 件；剩余物品保留原处。"),R.ActualQuantity):ResultText(R.Code)));}
void UAetherInventoryPage::SearchChanged(const FText& T)
{
    if(bUpdating)return;bDirty=true;SearchFilter=T.ToString().Left(256);
    if(Menu.IsValid()){auto M=Menu->GetPageMemory(EAetherMenuPage::Inventory);M.Search=SearchFilter;Menu->SavePageMemory(EAetherMenuPage::Inventory,M);}Refresh();
}
void UAetherInventoryPage::CategoryChanged(FString C,ESelectInfo::Type){if(bUpdating)return;bDirty=true;CategoryFilter=C==TEXT("全部")?FString():C;Refresh();}
void UAetherInventoryPage::Sort(){if(ModalToken.IsValid())return;FAetherPlayerCommand C;C.Type=EAetherCommandType::SortInventory;C.Enabled=true;Send(C);}
void UAetherInventoryPage::Retry(){if(Client.IsValid()){if(!Client->HasPending()||!Client->RetryPending())Client->RequestSnapshot();Client->RefreshContainer();}}
UWidget* UAetherInventoryPage::GetNavigationFocusTarget() const
{for(const auto& Cell:Cells)if(Cell&&!Cell->IsFiltered())return Cell.Get();return Search.Get();}
void UAetherInventoryPage::CycleRegion(int32 Direction)
{
    TArray<UWidget*> Regions;if(Search)Regions.Add(Search);if(Categories)Regions.Add(Categories);
    if(!EquipmentCells.IsEmpty())Regions.Add(EquipmentCells[0]);
    if(auto* Target=GetNavigationFocusTarget();Target&&Target!=Search)Regions.Add(Target);
    if(!ProductCells.IsEmpty())Regions.Add(ProductCells[0]);
    if(!ContainerCells.IsEmpty())Regions.Add(ContainerCells[0]);
    if(Details&&Session.GetDetails().IsSet())Regions.Add(Details->NavigationTarget());
    if(Regions.IsEmpty())return;
    int32 Current=INDEX_NONE;
    for(int32 N=0;N<Regions.Num();++N)
    {
        UWidget* Root=Regions[N];
        if(auto* Cell=Cast<UAetherInventoryCell>(Root))Root=Cell->GetParent();
        if(Root&&(Root->HasUserFocus(GetOwningPlayer())||Root->HasUserFocusedDescendants(GetOwningPlayer())))Current=N;
        if(Details&&Regions[N]==Details->NavigationTarget()&&Details->HasUserFocusedDescendants(GetOwningPlayer()))Current=N;
    }
    auto* Target=Regions[(Current+Direction+Regions.Num())%Regions.Num()];Target->SetUserFocus(GetOwningPlayer());
    for(auto* Parent=Target->GetParent();Parent;Parent=Parent->GetParent())if(auto* Scroll=Cast<UScrollBox>(Parent)){Scroll->ScrollWidgetIntoView(Target,true);break;}
}
FReply UAetherInventoryPage::NativeOnPreviewKeyDown(const FGeometry& G,const FKeyEvent& E)
{
    if(!ModalToken.IsValid()&&(E.GetKey()==EKeys::Tab||E.GetKey()==EKeys::Gamepad_LeftTrigger||E.GetKey()==EKeys::Gamepad_RightTrigger))
    {if(!E.IsRepeat())CycleRegion(E.IsShiftDown()||E.GetKey()==EKeys::Gamepad_LeftTrigger?-1:1);return FReply::Handled();}
    if(E.GetKey()==EKeys::Escape||E.GetKey()==EKeys::Gamepad_FaceButton_Right)
    {if(Session.Back()){HideConfirmation();RenderDetails();return FReply::Handled();}if(Menu.IsValid())Menu->Back();return FReply::Handled();}
    return Super::NativeOnPreviewKeyDown(G,E);
}
