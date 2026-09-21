#pragma once
#include "Blueprint/UserWidget.h"
#include "AetherPlayerHUDWidget.generated.h"
class UProgressBar;
class UTextBlock;
class UVerticalBox;
class UBorder;
UCLASS()
class AETHERUI_API UAetherPlayerHUDWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
private:
    void Refresh();
    FTimerHandle Timer;
    UPROPERTY() TArray<TObjectPtr<UProgressBar>> Bars;
    UPROPERTY() TArray<TObjectPtr<UTextBlock>> Vitals;
    UPROPERTY() TArray<TObjectPtr<UTextBlock>> Skills;
    UPROPERTY() TObjectPtr<UTextBlock> Guidance;
    UPROPERTY() TObjectPtr<UTextBlock> Interaction;
    UPROPERTY() TObjectPtr<UTextBlock> Feedback;
    UPROPERTY() TObjectPtr<UTextBlock> State;
    UPROPERTY() TObjectPtr<UTextBlock> Encounter;
    UPROPERTY() TObjectPtr<UBorder> Crosshair;
    FString LastFeedback;
    double FeedbackUntil=0;
};
