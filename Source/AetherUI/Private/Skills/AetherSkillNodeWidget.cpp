#include "Skills/AetherSkillNodeWidget.h"
#include "UI/AetherWidgetAssets.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/VerticalBox.h"
#include "Components/HorizontalBox.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/SizeBox.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/Texture2D.h"
TSharedRef<SWidget> UAetherSkillNodeWidget::RebuildWidget()
{
    if(!WidgetTree)WidgetTree=NewObject<UWidgetTree>(this);
    if(!WidgetTree->RootWidget)
    {
        auto* Card=WidgetTree->ConstructWidget<UBorder>();Card->SetPadding(FMargin(6));Card->SetBrushColor(FLinearColor(.055,.07,.1));WidgetTree->RootWidget=Card;
        auto* Rows=WidgetTree->ConstructWidget<UVerticalBox>();Card->SetContent(Rows);
        auto* Heading=WidgetTree->ConstructWidget<UHorizontalBox>();Rows->AddChildToVerticalBox(Heading);
        SkillIcon=WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),TEXT("SkillIcon"));SkillIcon->SetDesiredSizeOverride(FVector2D(32,32));Heading->AddChild(SkillIcon);
        SkillTitle=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("SkillTitle"));SkillTitle->SetAutoWrapText(true);Heading->AddChild(SkillTitle);
        SkillState=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("SkillState"));Rows->AddChild(SkillState);
        SkillSource=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("SkillSource"));Rows->AddChild(SkillSource);
        for(auto* Label:{SkillTitle.Get(),SkillState.Get(),SkillSource.Get()}){auto Font=Label->GetFont();Font.Size=13;Label->SetFont(Font);}
    }
    SetVisibility(ESlateVisibility::HitTestInvisible);RefreshNode();return Super::RebuildWidget();
}
void UAetherSkillNodeWidget::SetNode(const FAetherSkillTreeNode& Value){Node=Value;RefreshNode();}
void UAetherSkillNodeWidget::RefreshNode()
{
    if(!SkillTitle||!SkillState||!SkillSource||!SkillIcon)return;
    SkillTitle->SetText(FText::FromString(Node.Title+FString::Printf(TEXT(" %d"),Node.Identity.Rank)));
    SkillState->SetText(FText::FromString(AetherSkillTree::StateLabel(Node.State)));
    SkillSource->SetText(FText::FromString(Node.bPermanent?TEXT("✓ 永久授权"):Node.bAuthorized?TEXT("◇ 外部授权"):TEXT("○ 未学习")));
    const FLinearColor Color=Node.bAuthorized?FLinearColor(.7,1,.8):FLinearColor(.7,.76,.84);
    SkillState->SetColorAndOpacity(Color);SkillSource->SetColorAndOpacity(Color);
    if(LoadedIcon==Node.IconId)return;LoadedIcon=Node.IconId;const uint32 Generation=++IconGeneration;
    SkillIcon->SetBrushFromTexture(nullptr);const auto Path=AetherWidgetAssets::Icon(LoadedIcon);
    const TWeakObjectPtr<UAetherSkillNodeWidget> Self=this;
    if(Path.IsValid())UAssetManager::GetStreamableManager().RequestAsyncLoad(Path,[Self,Path,Generation]()
    {
        if(!Self.IsValid()||Self->IconGeneration!=Generation)return;
        auto* Texture=Cast<UTexture2D>(Path.ResolveObject());Self->SkillIcon->SetBrushFromTexture(Texture);
        if(!Texture)UE_LOG(LogTemp,Error,TEXT("AETHER_SKILL_ICON_MISSING %s"),*Path.ToString());
    });
}
