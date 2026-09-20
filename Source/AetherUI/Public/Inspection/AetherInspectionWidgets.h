#pragma once
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Inspection/AetherInspectionSession.h"
#include "AetherInspectionWidgets.generated.h"

class UVerticalBox;
class UImage;
class UTextBlock;
class USpinBox;
class UTexture2D;
DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnAetherInspectionButton,const FAetherInspectRequest&,const FAetherInspectionAction&,bool);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAetherInspectionAction,const FAetherInspectRequest&,const FAetherInspectionAction&);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAetherComparisonRequested,const FAetherInspectRequest&,const FString&);
DECLARE_MULTICAST_DELEGATE(FOnAetherInspectionDismissed);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAetherInspectionConfirmed,FGuid,int32);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnAetherInspectionCancelled,FGuid);

// 每个按钮捕获值身份，不能在点击时读取可能已复用成相邻物品的列表索引。
UCLASS()
class AETHERUI_API UAetherInspectionActionButton : public UButton
{
    GENERATED_BODY()
public:
    void InitializeAction(const FAetherInspectRequest& Request,const FAetherInspectionAction& Action,bool Comparison=false);
    FOnAetherInspectionButton OnRequested;
private:
    UFUNCTION() void Dispatch();
    FAetherInspectRequest Request;
    FAetherInspectionAction Action;
    bool bComparison=false;
};

// 可停留和滚动的详情卡。只发意图，宿主必须交给 InspectionSession 与统一命令服务。
UCLASS()
class AETHERUI_API UAetherInspectionCard : public UUserWidget
{
    GENERATED_BODY()
public:
    void SetModel(const FAetherInspectionModel& Model);
    uint64 GetDisplayGeneration() const {return Generation;}
    // 图标解析器异步加载后带回代数；迟到资源不能覆盖后来查看的对象。
    void SetResolvedIcon(uint64 DisplayGeneration,UTexture2D* Texture);
    FOnAetherInspectionAction OnActionRequested;
    FOnAetherComparisonRequested OnComparisonRequested;
    FOnAetherInspectionDismissed OnDismissRequested;
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual FReply NativeOnMouseButtonDown(const FGeometry& Geometry,const FPointerEvent& Event) override;
    virtual FReply NativeOnKeyDown(const FGeometry& Geometry,const FKeyEvent& Event) override;
    virtual void NativeDestruct() override;
private:
    void RenderModel();
    void Forward(const FAetherInspectRequest& Request,const FAetherInspectionAction& Action,bool Comparison);
    void AddLine(const FString& Text,FLinearColor Color=FLinearColor::White);
    void AddEffect(const TCHAR* Label,const FAetherSkillRankEffect& Effect);
    FAetherInspectionModel Model;
    uint64 Generation=0;
    UPROPERTY(Transient) TObjectPtr<UVerticalBox> Rows;
    UPROPERTY(Transient) TObjectPtr<UImage> Icon;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Heading;
};

// 数量/危险确认是详情上层独立控件。宿主按令牌关闭本层，不能把它伪装成 hover。
UCLASS()
class AETHERUI_API UAetherInspectionConfirmation : public UUserWidget
{
    GENERATED_BODY()
public:
    void SetDraft(const FAetherInspectionDraft& Draft);
    void InvalidateDraft();
    FOnAetherInspectionConfirmed OnConfirmed;
    FOnAetherInspectionCancelled OnCancelled;
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual FReply NativeOnMouseButtonDown(const FGeometry& Geometry,const FPointerEvent& Event) override;
    virtual FReply NativeOnKeyDown(const FGeometry& Geometry,const FKeyEvent& Event) override;
    virtual void NativeDestruct() override;
private:
    void RefreshDraft();
    UFUNCTION() void QuantityChanged(float Value);
    UFUNCTION() void Confirm();
    UFUNCTION() void Cancel();
    TOptional<FAetherInspectionDraft> Draft;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Prompt;
    UPROPERTY(Transient) TObjectPtr<USpinBox> Quantity;
    UPROPERTY(Transient) TObjectPtr<UButton> ConfirmButton;
};
