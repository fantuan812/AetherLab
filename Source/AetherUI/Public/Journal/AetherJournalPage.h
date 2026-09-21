#pragma once
#include "UI/AetherPageBase.h"
#include "AetherJournalPage.generated.h"
class UVerticalBox;
class UTextBlock;
class UAetherInspectionCard;
UCLASS()
class AETHERUI_API UAetherJournalPage : public UAetherPageBase
{
    GENERATED_BODY()
public:
    UAetherJournalPage(){Page=EAetherMenuPage::Journal;}
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void RefreshPage() override;
    virtual void LiveRefresh() override;
    virtual UWidget* InitialFocus() const override;
private:
    void Select(FName Quest);
    void Claim(FGuid Reward,int64 Seen);
    void Inspect(const FString& Item);
    void ShowDetails();
    int32 Filter=0;
    FName Selected;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UVerticalBox> List;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UVerticalBox> Details;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UVerticalBox> Rewards;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> Notice;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> RefreshClock;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UAetherInspectionCard> ItemCard;
    UPROPERTY() TObjectPtr<UWidget> First;
};
