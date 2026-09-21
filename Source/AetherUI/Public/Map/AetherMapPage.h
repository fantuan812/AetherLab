#pragma once
#include "UI/AetherPageBase.h"
#include "AetherMapPage.generated.h"
class UTextBlock;
class UVerticalBox;
struct FAetherMapMarker
{
    FString Id,Label;FVector Position=FVector::ZeroVector;FLinearColor Color=FLinearColor::White;
    FName Beacon;bool bPlayer=false;float Yaw=0;
};
DECLARE_DELEGATE_OneParam(FOnAetherMapMarker,const FAetherMapMarker&);
UCLASS()
class AETHERUI_API UAetherMapCanvas : public UUserWidget
{
    GENERATED_BODY()
public:
    TArray<FAetherMapMarker> Markers;
    FOnAetherMapMarker Selected;
    float Zoom=1;FVector2D Pan=FVector2D::ZeroVector;
    void ChangeZoom(float Factor){Zoom=FMath::Clamp(Zoom*Factor,.6f,4.f);InvalidateLayoutAndVolatility();}
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual int32 NativePaint(const FPaintArgs& Args,const FGeometry& G,const FSlateRect& Clip,FSlateWindowElementList& Out,int32 Layer,const FWidgetStyle& Style,bool Enabled) const override;
    virtual FReply NativeOnMouseWheel(const FGeometry& G,const FPointerEvent& E) override;
    virtual FReply NativeOnMouseButtonDown(const FGeometry& G,const FPointerEvent& E) override;
    virtual FReply NativeOnMouseButtonUp(const FGeometry& G,const FPointerEvent& E) override;
    virtual FReply NativeOnMouseMove(const FGeometry& G,const FPointerEvent& E) override;
    virtual FReply NativeOnKeyDown(const FGeometry& G,const FKeyEvent& E) override;
private:
    FVector2D Point(const FGeometry& G,FVector2D World) const;
    int32 SelectedIndex=0;
    bool bDrag=false;
};
UCLASS()
class AETHERUI_API UAetherMapPage : public UAetherPageBase
{
    GENERATED_BODY()
public:
    UAetherMapPage(){Page=EAetherMenuPage::Map;}
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void RefreshPage() override;
    virtual void LiveRefresh() override;
    virtual UWidget* InitialFocus() const override;
private:
    void Select(const FAetherMapMarker& Marker);
    void Travel();
    bool bServices=true,bParty=true,bQuests=true;
    FName SelectedBeacon;
    UPROPERTY() TObjectPtr<UAetherMapCanvas> Map;
    UPROPERTY() TObjectPtr<UTextBlock> Detail;
    UPROPERTY() TObjectPtr<UAetherPageButton> TravelButton;
};
