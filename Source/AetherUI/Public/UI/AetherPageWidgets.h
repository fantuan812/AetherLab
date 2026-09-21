#pragma once
#include "UI/AetherPageBase.h"
#include "UI/AetherUITheme.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/Border.h"
namespace AetherPageWidgets
{
inline UTextBlock* Text(UWidgetTree& Tree,UVerticalBox& Rows,const FString& Label,FLinearColor Color=FLinearColor(.86f,.9f,.95f))
{
    auto* T=Tree.ConstructWidget<UTextBlock>();T->SetText(FText::FromString(Label));T->SetAutoWrapText(true);T->SetColorAndOpacity(Color);
    auto Font=T->GetFont();Font.Size=UAetherUITheme::Get().BodySize;T->SetFont(Font);
    Rows.AddChildToVerticalBox(T)->SetPadding(FMargin(4,5));return T;
}
inline UAetherPageButton* Button(UWidgetTree& Tree,UVerticalBox& Rows,const FString& Label,FSimpleDelegate Action,bool Enabled=true)
{
    auto* B=Tree.ConstructWidget<UAetherPageButton>();auto* T=Tree.ConstructWidget<UTextBlock>();T->SetText(FText::FromString(Label));T->SetAutoWrapText(true);
    B->SetContent(T);B->SetIsEnabled(Enabled);B->Bind(MoveTemp(Action));Rows.AddChildToVerticalBox(B)->SetPadding(FMargin(3,5));return B;
}
inline UVerticalBox* Card(UWidgetTree& Tree,UVerticalBox& Rows,FLinearColor Color=FLinearColor(.045f,.065f,.09f))
{
    auto* B=Tree.ConstructWidget<UBorder>();B->SetBrushColor(Color);B->SetPadding(FMargin(UAetherUITheme::Get().Padding));
    Rows.AddChildToVerticalBox(B)->SetPadding(FMargin(0,5));auto* V=Tree.ConstructWidget<UVerticalBox>();B->SetContent(V);return V;
}
}
