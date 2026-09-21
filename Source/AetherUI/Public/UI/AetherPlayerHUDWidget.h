#pragma once
#include "Blueprint/UserWidget.h"
#include "AetherPlayerHUDWidget.generated.h"
class UProgressBar;
class UImage;
class UTextBlock;
class UVerticalBox;
class UHorizontalBox;
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
    void BuildContents();
    FTimerHandle Timer;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UHorizontalBox> EffectsRow;
    TArray<FGuid> ShownEffects;
    UPROPERTY() TArray<TObjectPtr<UTextBlock>> EffectLabels;
    UPROPERTY() TArray<TObjectPtr<UProgressBar>> Bars;
    UPROPERTY() TArray<TObjectPtr<UTextBlock>> Vitals;
    UPROPERTY() TArray<TObjectPtr<UTextBlock>> Skills;
    UPROPERTY() TArray<TObjectPtr<UImage>> SkillIcons;
    UPROPERTY() TArray<TObjectPtr<UProgressBar>> Cooldowns;
    TArray<FString> ShownIcons;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> EquipmentText;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> TargetName;
    UPROPERTY() TObjectPtr<UProgressBar> TargetHealth;
    UPROPERTY() TObjectPtr<UWidget> TargetPanel;
    UPROPERTY() TObjectPtr<UWidget> PromptPanel;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> Guidance;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> Interaction;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> Feedback;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> State;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> Encounter;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UBorder> Crosshair;
    FString LastFeedback;
    double FeedbackUntil=0;
};
