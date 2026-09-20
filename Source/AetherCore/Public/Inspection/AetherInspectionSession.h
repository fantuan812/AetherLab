#pragma once
#include "Inspection/AetherInspectionService.h"
#include "Contracts/AetherPlayerCommand.h"

// 这层只管理展示与用户意图；不保存引用 UObject，也不执行装备、学习或网络发送。
struct FAetherInspectionDraft
{
    FGuid Token;
    FAetherInspectRequest Request;
    FAetherInspectionAction Action;
};
struct FAetherInspectionDispatch
{
    // 本地“定位前置/追踪任务”和持久命令必须分开，不能把导航变成伪造服务器交互。
    EAetherInspectAction Action=EAetherInspectAction::Equip;
    FString NavigationTarget;
    TOptional<FAetherPlayerCommand> Command;
    TArray<uint8> CommandBytes; // 命令服务重试应保留这些字节，不根据新 UI 状态重建请求。
};
class AETHERCORE_API FAetherInspectionSession
{
public:
    void ShowHover(FAetherInspectTarget Target,const FAetherInspectionSnapshot& S,const FAetherV10ItemDefinitions& I,const FAetherSkillDefinitionsV10& K);
    void HideHover(){Hover.Reset();}
    void OpenDetails(FAetherInspectTarget Target,const FAetherInspectionSnapshot& S,const FAetherV10ItemDefinitions& I,const FAetherSkillDefinitionsV10& K);
    void Refresh(const FAetherInspectionSnapshot& S,const FAetherV10ItemDefinitions& I,const FAetherSkillDefinitionsV10& K);
    // 关页不丢弃待确认命令；拥有者会话销毁前由统一命令服务接管重试/回执。
    void Close();
    bool Acknowledge(const FAetherCommandResult& Result);
    const TOptional<FAetherInspectionDispatch>& GetPendingDispatch() const{return Pending;}
    // Esc 的顺序：数量/危险确认 -> 持续详情 -> 悬停；返回 false 后由菜单关闭页面。
    bool Back();
    const TOptional<FAetherInspectionModel>& GetHover() const{return Hover;}
    const TOptional<FAetherInspectionModel>& GetDetails() const{return Details;}
    const TOptional<FAetherInspectionDraft>& GetDraft() const{return Draft;}
    FGuid BeginAction(EAetherInspectAction Action,const FString& Argument);
    bool Confirm(FGuid Token,int32 Quantity,const FAetherInspectionSnapshot& S,const FAetherV10ItemDefinitions& I,
        const FAetherSkillDefinitionsV10& K,FAetherInspectionDispatch& Out,FString& Reason);
    // 输入坐标必须均为同一 DPI 下的逻辑坐标。先向左/上翻转，再夹取到可见边缘。
    static FVector2D PlacePopup(FVector2D Anchor,FVector2D Desired,FVector2D Viewport,float Margin=8.f);
private:
    TOptional<FAetherInspectionModel> Hover,Details;
    TOptional<FAetherInspectionDraft> Draft;
    TOptional<FAetherInspectionDispatch> Pending;
    TOptional<FAetherCommandResult> Receipt;
    FAetherInspectContext PendingContext;
    void MarkPending();
};
