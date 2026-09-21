#include "UI/AetherWidgetAssets.h"
#include "Blueprint/UserWidget.h"
namespace AetherWidgetAssets
{
UClass* Resolve(UClass* NativeClass,const TCHAR* Variant)
{
    if(!NativeClass)return nullptr;
    // 同一张表供运行时解析和资产作者脚本读取，禁止两个路径列表独立漂移。
    const TPair<const TCHAR*,const TCHAR*> Assets[]={
        {TEXT("AetherMenuRoot"),TEXT("WBP_MenuRoot")},
        {TEXT("AetherFrontierPanel"),TEXT("WBP_PlayerMenu")},
        {TEXT("AetherInventoryPage"),TEXT("WBP_InventoryPage")},
        {TEXT("AetherSkillTreePage"),TEXT("WBP_SkillTreePage")},
        {TEXT("AetherJournalPage"),TEXT("WBP_JournalPage")},
        {TEXT("AetherMapPage"),TEXT("WBP_MapPage")},
        {TEXT("AetherPartyPage"),TEXT("WBP_PartyPage")},
        {TEXT("AetherSettingsPage"),TEXT("WBP_SettingsPage")},
        {TEXT("AetherDialoguePage"),TEXT("WBP_Dialogue")},
        {TEXT("AetherInventoryCell"),TEXT("WBP_ItemSlot")},
        {TEXT("AetherInspectionCard"),TEXT("WBP_InspectPanel")},
        {TEXT("AetherInspectionConfirmation"),TEXT("WBP_ConfirmDialog")},
        {TEXT("AetherCharacterPreviewWidget"),TEXT("WBP_CharacterPreview")},
        {TEXT("AetherPlayerHUDWidget"),TEXT("WBP_PlayerHUD")}
    };
    FString Name=Variant?Variant:TEXT("");
    if(Name.IsEmpty())for(const auto& Pair:Assets)if(NativeClass->GetName()==Pair.Key){Name=Pair.Value;break;}
    if(Name.IsEmpty())return NativeClass;
    const FString Path=TEXT("/Game/UI/Widgets/")+Name+TEXT(".")+Name+TEXT("_C");
    if(auto* Class=LoadClass<UUserWidget>(nullptr,*Path);Class&&Class->IsChildOf(NativeClass))return Class;
    // 原生父类仅保留编辑器重建能力；缺资源必须在内容验收中明确失败。
    static TSet<FString> Reported;
    if(!Reported.Contains(Path)){Reported.Add(Path);UE_LOG(LogTemp,Error,TEXT("AETHER_UI_ASSET_MISSING %s"),*Path);}
    return NativeClass;
}
FSoftObjectPath Icon(const FString& StableId)
{
    FString Name=StableId;Name.ReplaceInline(TEXT("."),TEXT("_"));
    for(TCHAR C:Name)if(!FChar::IsAlnum(C)&&C!='_')return {};
    return Name.IsEmpty()?FSoftObjectPath():FSoftObjectPath(TEXT("/Game/UI/Icons/T_")+Name+TEXT(".T_")+Name);
}
}
