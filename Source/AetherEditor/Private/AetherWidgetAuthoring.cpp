#include "AetherWidgetAuthoring.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/OverlaySlot.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/UniformGridPanel.h"
#include "Components/EditableTextBox.h"
#include "Components/ComboBoxString.h"
#include "Components/SpinBox.h"
#include "Components/InputKeySelector.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "Editor.h"
namespace
{
FMargin Padding(const TSharedPtr<FJsonObject>& D,const TCHAR* Key)
{
    const TArray<TSharedPtr<FJsonValue>>* Values=nullptr;
    if(!D->TryGetArrayField(Key,Values)||Values->Num()!=4)return FMargin(0);
    return FMargin((*Values)[0]->AsNumber(),(*Values)[1]->AsNumber(),(*Values)[2]->AsNumber(),(*Values)[3]->AsNumber());
}
UWidget* Build(UWidgetTree& Tree,const TSharedPtr<FJsonObject>& D,TSet<FName>& Names,int32& Count,int32 Depth,FString& Why)
{
    if(!D.IsValid()||++Count>256||Depth>20){Why=TEXT("Layout exceeds bounds");return nullptr;}
    FString ClassPath,Name;
    if(!D->TryGetStringField(TEXT("Class"),ClassPath)||!D->TryGetStringField(TEXT("Name"),Name)||
       Name.IsEmpty()||Name.Len()>80||Names.Contains(FName(*Name)))
    {Why=TEXT("Invalid or duplicate layout node");return nullptr;}
    if(!ClassPath.StartsWith(TEXT("/Script/UMG."))&&!ClassPath.StartsWith(TEXT("/Script/AetherUI."))&&!ClassPath.StartsWith(TEXT("/Script/CommonUI."))&&
       !ClassPath.StartsWith(TEXT("/Game/UI/Widgets/WBP_"))){Why=TEXT("Layout class outside UI allowlist");return nullptr;}
    UClass* Class=LoadClass<UWidget>(nullptr,*ClassPath);
    if(!Class||Class->HasAnyClassFlags(CLASS_Abstract)){Why=TEXT("Missing concrete widget class: ")+ClassPath;return nullptr;}
    Names.Add(FName(*Name));UWidget* W=Tree.ConstructWidget<UWidget>(Class,FName(*Name));if(!W)return nullptr;
    W->bIsVariable=true;
    FString Text;D->TryGetStringField(TEXT("Text"),Text);
    if(auto* Label=Cast<UTextBlock>(W))
    {
        Label->SetText(FText::FromString(Text));Label->SetAutoWrapText(true);
        double Size=16;D->TryGetNumberField(TEXT("FontSize"),Size);auto Font=Label->GetFont();Font.Size=FMath::Clamp(int32(Size),10,40);Label->SetFont(Font);
        Label->SetColorAndOpacity(FLinearColor(.85,.89,.95));
    }
    if(auto* Edit=Cast<UEditableTextBox>(W))Edit->SetHintText(FText::FromString(Text));
    const TArray<TSharedPtr<FJsonValue>>* Color=nullptr;
    if(D->TryGetArrayField(TEXT("Color"),Color)&&Color->Num()==4)
    {
        const FLinearColor C((*Color)[0]->AsNumber(),(*Color)[1]->AsNumber(),(*Color)[2]->AsNumber(),(*Color)[3]->AsNumber());
        if(auto* Border=Cast<UBorder>(W))Border->SetBrushColor(C);
        if(auto* Label=Cast<UTextBlock>(W))Label->SetColorAndOpacity(C);
        if(auto* Button=Cast<UButton>(W))Button->SetBackgroundColor(C);
    }
    if(auto* Border=Cast<UBorder>(W))Border->SetPadding(Padding(D,TEXT("Padding")));
    if(auto* Grid=Cast<UUniformGridPanel>(W))Grid->SetSlotPadding(FMargin(3));
    const TArray<TSharedPtr<FJsonValue>>* Size=nullptr;
    if(D->TryGetArrayField(TEXT("Size"),Size)&&Size->Num()==2)
    {
        const float X=(*Size)[0]->AsNumber(),Y=(*Size)[1]->AsNumber();
        if(auto* Box=Cast<USizeBox>(W)){if(X>0)Box->SetWidthOverride(X);if(Y>0)Box->SetHeightOverride(Y);}
        if(auto* Image=Cast<UImage>(W))Image->SetDesiredSizeOverride(FVector2D(X,Y));
    }
    double Number;
    if(auto* Box=Cast<USizeBox>(W))
    {
        if(D->TryGetNumberField(TEXT("MinWidth"),Number))Box->SetMinDesiredWidth(Number);
        if(D->TryGetNumberField(TEXT("MinHeight"),Number))Box->SetMinDesiredHeight(Number);
        if(D->TryGetNumberField(TEXT("MaxWidth"),Number))Box->SetMaxDesiredWidth(Number);
        if(D->TryGetNumberField(TEXT("MaxHeight"),Number))Box->SetMaxDesiredHeight(Number);
    }
    if(auto* Spin=Cast<USpinBox>(W))
    {
        if(D->TryGetNumberField(TEXT("Min"),Number))Spin->SetMinValue(Number);
        if(D->TryGetNumberField(TEXT("Max"),Number))Spin->SetMaxValue(Number);
        if(D->TryGetNumberField(TEXT("Step"),Number))Spin->SetDelta(Number);
        if(D->TryGetNumberField(TEXT("Digits"),Number)){Spin->SetMinFractionalDigits(Number);Spin->SetMaxFractionalDigits(Number);}
    }
    if(auto* Combo=Cast<UComboBoxString>(W))
    {
        const TArray<TSharedPtr<FJsonValue>>* Options=nullptr;
        if(D->TryGetArrayField(TEXT("Options"),Options))for(const auto& Option:*Options)Combo->AddOption(Option->AsString());
    }
    if(auto* Key=Cast<UInputKeySelector>(W)){Key->SetAllowModifierKeys(false);Key->SetAllowGamepadKeys(false);}
    const TArray<TSharedPtr<FJsonValue>>* Children=nullptr;
    if(D->TryGetArrayField(TEXT("Children"),Children))
    {
        auto* Panel=Cast<UPanelWidget>(W);if(!Panel){Why=TEXT("Leaf widget cannot contain children");return nullptr;}
        for(const auto& Child:*Children)
        {
            const auto ChildData=Child->AsObject();auto* C=Build(Tree,ChildData,Names,Count,Depth+1,Why);if(!C)return nullptr;
            auto* Slot=Panel->AddChild(C);if(!Slot){Why=TEXT("Widget does not accept another child");return nullptr;}
            double Fill=0;const bool Filling=ChildData->TryGetNumberField(TEXT("Fill"),Fill);
            FSlateChildSize Rule(Filling?ESlateSizeRule::Fill:ESlateSizeRule::Automatic);Rule.Value=FMath::Clamp(float(Fill),.01f,10.f);
            const auto Margin=Padding(ChildData,TEXT("SlotPadding"));
            if(auto* O=Cast<UOverlaySlot>(Slot)){O->SetHorizontalAlignment(HAlign_Fill);O->SetVerticalAlignment(VAlign_Fill);O->SetPadding(Margin);}
            if(auto* Canvas=Cast<UCanvasPanelSlot>(Slot))
            {
                const TArray<TSharedPtr<FJsonValue>>* Values=nullptr;
                if(ChildData->TryGetArrayField(TEXT("Anchor"),Values)&&Values->Num()==2)Canvas->SetAnchors(FAnchors((*Values)[0]->AsNumber(),(*Values)[1]->AsNumber()));
                if(ChildData->TryGetArrayField(TEXT("Align"),Values)&&Values->Num()==2)Canvas->SetAlignment(FVector2D((*Values)[0]->AsNumber(),(*Values)[1]->AsNumber()));
                if(ChildData->TryGetArrayField(TEXT("Position"),Values)&&Values->Num()==2)Canvas->SetPosition(FVector2D((*Values)[0]->AsNumber(),(*Values)[1]->AsNumber()));
                if(ChildData->TryGetArrayField(TEXT("Size"),Values)&&Values->Num()==2)Canvas->SetSize(FVector2D((*Values)[0]->AsNumber(),(*Values)[1]->AsNumber()));
            }
            if(auto* H=Cast<UHorizontalBoxSlot>(Slot)){H->SetSize(Rule);H->SetPadding(Margin);}
            if(auto* V=Cast<UVerticalBoxSlot>(Slot)){V->SetSize(Rule);V->SetPadding(Margin);}
        }
    }
    bool Hidden=false;if(D->TryGetBoolField(TEXT("Hidden"),Hidden)&&Hidden)W->SetVisibility(ESlateVisibility::Collapsed);
    return W;
}
}
bool UAetherWidgetAuthoring::ApplyLayout(UWidgetBlueprint* Blueprint,const FString& Json,FString& Reason)
{
    if(!Blueprint||Json.Len()>128*1024||(GEditor&&GEditor->PlayWorld)){Reason=TEXT("Invalid authoring context");return false;}
    TSharedPtr<FJsonObject> Document;
    if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Document)||!Document.IsValid()){Reason=TEXT("Invalid layout JSON");return false;}
    // 先完整构建候选，失败时不改变已有资产；调用者明确选择此资源时才替换正式 Designer 树。
    auto* Candidate=NewObject<UWidgetTree>(GetTransientPackage());TSet<FName> Names;int32 Count=0;
    Candidate->RootWidget=Build(*Candidate,Document,Names,Count,0,Reason);if(!Candidate->RootWidget)return false;
    Blueprint->Modify();
    if(Blueprint->WidgetTree)Blueprint->WidgetTree->Rename(nullptr,GetTransientPackage(),REN_DontCreateRedirectors);
    Candidate->Rename(TEXT("WidgetTree"),Blueprint,REN_DontCreateRedirectors);Candidate->SetFlags(RF_Transactional);Blueprint->WidgetTree=Candidate;
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);FKismetEditorUtilities::CompileBlueprint(Blueprint);
    if(Blueprint->Status==BS_Error){Reason=TEXT("Widget Blueprint compilation failed: ")+Blueprint->GetName();return false;}
    Blueprint->MarkPackageDirty();FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;
    const auto Filename=FPackageName::LongPackageNameToFilename(Blueprint->GetOutermost()->GetName(),FPackageName::GetAssetPackageExtension());
    if(!UPackage::SavePackage(Blueprint->GetOutermost(),Blueprint,*Filename,Args)){Reason=TEXT("Cannot save widget layout");return false;}
    return true;
}
