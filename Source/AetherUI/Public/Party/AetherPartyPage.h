#pragma once
#include "UI/AetherPageBase.h"
#include "AetherPartyPage.generated.h"
class UVerticalBox;
class UTextBlock;
class AAetherPlayerState;
UCLASS()
class AETHERUI_API UAetherPartyPage : public UAetherPageBase
{
    GENERATED_BODY()
public:
    UAetherPartyPage(){Page=EAetherMenuPage::Party;}
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void RefreshPage() override;
    virtual void LiveRefresh() override;
    virtual UWidget* InitialFocus() const override;
private:
    void Act(FName Action,TWeakObjectPtr<AAetherPlayerState> Target={},TWeakObjectPtr<AAetherFrontierCharacter> Companion={});
    FString LastRoster;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UVerticalBox> Members;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UVerticalBox> Nearby;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UVerticalBox> Invitations;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> Notice;
    UPROPERTY() TObjectPtr<UWidget> First;
    struct FHealthRow{TWeakObjectPtr<AAetherFrontierCharacter> Pawn;TWeakObjectPtr<UTextBlock> Text;};
    TArray<FHealthRow> HealthRows;
};
