#include "UI/AetherHUDSection.h"
#include "UI/AetherUITheme.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/VerticalBox.h"
TSharedRef<SWidget> UAetherHUDSection::RebuildWidget()
{
    if(!WidgetTree)WidgetTree=NewObject<UWidgetTree>(this);
    if(!WidgetTree->RootWidget)
    {
        auto* Border=WidgetTree->ConstructWidget<UBorder>();Border->SetBrushColor(UAetherUITheme::Get().Panel);
        Border->SetPadding(FMargin(UAetherUITheme::Get().Padding));WidgetTree->RootWidget=Border;
        Rows=WidgetTree->ConstructWidget<UVerticalBox>();Border->SetContent(Rows);
    }
    return Super::RebuildWidget();
}
