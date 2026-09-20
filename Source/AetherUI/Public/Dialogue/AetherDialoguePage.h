#pragma once
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "AetherDialoguePage.generated.h"
class UAetherDialogueSession;
class AAetherFrontierCharacter;
class UTextBlock;
class UVerticalBox;
DECLARE_DELEGATE_TwoParams(FOnAetherDialogueChoice,int32,uint64);

UCLASS()
class AETHERUI_API UAetherDialogueChoiceButton : public UButton
{
    GENERATED_BODY()
public:
    void BindChoice(int32 Index,uint64 Version);
    FOnAetherDialogueChoice OnChoice;
private:
    int32 ChoiceIndex=INDEX_NONE;
    uint64 ShownVersion=0;
    UFUNCTION() void Select();
};

// 独立对话页只消费会话视图，不写任务、不生成目标、不决定服务资格。
UCLASS()
class AETHERUI_API UAetherDialoguePage : public UUserWidget
{
    GENERATED_BODY()
public:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual FReply NativeOnKeyDown(const FGeometry& Geometry,const FKeyEvent& Event) override;
private:
    void Refresh();
    void RefreshFeedback();
    TWeakObjectPtr<AAetherFrontierCharacter> Player;
    void Choose(int32 Index,uint64 Version);
    TWeakObjectPtr<UAetherDialogueSession> Session;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Speaker;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Speech;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Feedback;
    UPROPERTY(Transient) TObjectPtr<UVerticalBox> Choices;
};
