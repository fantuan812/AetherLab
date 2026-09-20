#include "Inspection/AetherInspectionSession.h"
#include "Contracts/AetherTransaction.h"

void FAetherInspectionSession::ShowHover(FAetherInspectTarget T,const FAetherInspectionSnapshot& S,const FAetherV10ItemDefinitions& I,const FAetherSkillDefinitionsV10& K)
{
    Hover=AetherInspection::Build(AetherInspection::Pin(S,MoveTemp(T)),S,I,K);
    // 悬停是摘要，不能挂可点击操作；持续详情和确认框有独立生命周期。
    Hover->Actions.Reset();
}
void FAetherInspectionSession::OpenDetails(FAetherInspectTarget T,const FAetherInspectionSnapshot& S,const FAetherV10ItemDefinitions& I,const FAetherSkillDefinitionsV10& K)
{
    Hover.Reset();Draft.Reset();Details=AetherInspection::Build(AetherInspection::Pin(S,MoveTemp(T)),S,I,K);MarkPending();
}
void FAetherInspectionSession::Refresh(const FAetherInspectionSnapshot& S,const FAetherV10ItemDefinitions& I,const FAetherSkillDefinitionsV10& K)
{
    // 成功回执可能早于拥有者属性复制。只有同一拥有者会话发布了提交版本才解除等待。
    if(Pending.IsSet()&&Receipt.IsSet()&&S.Context.SessionId==PendingContext.SessionId&&
        S.Context.OwnerIdentity.Equals(PendingContext.OwnerIdentity,ESearchCase::CaseSensitive)&&
        S.ProfileRevision>=Receipt->FinalProfileRevision){Pending.Reset();Receipt.Reset();}
    if(Hover.IsSet())
    {
        Hover=AetherInspection::Build(Hover->Request,S,I,K);Hover->Actions.Reset();
        if(!Hover->CanInteract())Hover.Reset();
    }
    if(Details.IsSet())
    {
        // 保留“对象已变化”解释，不自动 Pin 到新版本或同一格子的替代物品。
        Details=AetherInspection::Build(Details->Request,S,I,K);
        if(!Details->CanInteract())Draft.Reset();
    }
    MarkPending();
}
void FAetherInspectionSession::Close(){Hover.Reset();Draft.Reset();Details.Reset();}
bool FAetherInspectionSession::Back()
{
    if(Draft.IsSet()){Draft.Reset();return true;}
    if(Details.IsSet()){Details.Reset();return true;}
    if(Hover.IsSet()){Hover.Reset();return true;}
    return false;
}
FGuid FAetherInspectionSession::BeginAction(EAetherInspectAction Kind,const FString& Argument)
{
    if(Pending.IsSet()||Draft.IsSet()||!Details.IsSet()||!Details->CanInteract())return {};
    const auto* Action=Details->Actions.FindByPredicate([&](const auto& A)
        {return A.Kind==Kind&&A.Argument.Equals(Argument,ESearchCase::CaseSensitive);});
    if(!Action||!Action->bEnabled)return {};
    Draft=FAetherInspectionDraft{FGuid::NewGuid(),Details->Request,*Action};return Draft->Token;
}
bool FAetherInspectionSession::Confirm(FGuid Token,int32 Quantity,const FAetherInspectionSnapshot& S,const FAetherV10ItemDefinitions& I,
    const FAetherSkillDefinitionsV10& K,FAetherInspectionDispatch& Out,FString& Reason)
{
    Out={};Reason.Reset();
    const auto Fail=[&](const TCHAR* Text){Reason=Text;return false;};
    if(Pending.IsSet())return Fail(TEXT("上一条请求仍待确认。"));
    if(!Token.IsValid()||!Draft.IsSet()||Draft->Token!=Token)return Fail(TEXT("确认已失效。"));
    const auto Current=AetherInspection::Build(Draft->Request,S,I,K);
    if(!Current.CanInteract()){Draft.Reset();Refresh(S,I,K);return Fail(TEXT("对象已变化，请重新查看。"));}
    const auto* A=Current.Actions.FindByPredicate([&](const auto& V)
        {return V.Kind==Draft->Action.Kind&&V.Argument.Equals(Draft->Action.Argument,ESearchCase::CaseSensitive);});
    if(!A||!A->bEnabled){Draft.Reset();return Fail(TEXT("当前条件不允许此操作。"));}
    if(Quantity<1||Quantity>A->MaxQuantity)return Fail(TEXT("数量超出当前对象允许范围。"));
    FAetherInspectionDispatch Result;Result.Action=A->Kind;
    using E=EAetherInspectAction;
    if(A->Kind==E::TrackQuest||A->Kind==E::FocusSkill)
        Result.NavigationTarget=A->Argument;
    else
    {
        FAetherPlayerCommand C;C.ProtocolVersion=AetherCommands::LatestProtocolVersion;
        C.CommandId=AetherTransactions::NewCommandId(S.ProfileRevision);C.ExpectedProfileRevision=S.ProfileRevision;
        // UI 令牌与协议命令身份分开：协议的 GUID 高位绑定档案版本，不能使用随机弹窗 GUID。
        // 确认成功后消费 UI 令牌；在 Pending 中保存本次命令和字节，所有重试复用它。
        const auto& Target=Draft->Request.Target;
        switch(A->Kind)
        {
        case E::Equip:C.Type=EAetherCommandType::EquipItem;C.ItemInstanceId=Target.InstanceId;C.SlotId=A->Argument;break;
        case E::Unequip:C.Type=EAetherCommandType::UnequipItem;C.ItemInstanceId=Target.InstanceId;break;
        case E::Drop:C.Type=EAetherCommandType::DropItem;C.ItemInstanceId=Target.InstanceId;C.Quantity=Quantity;C.ExpectedWorldRevision=S.WorldRevision;break;
        case E::Lock:case E::Unlock:C.Type=EAetherCommandType::SetItemLock;C.ItemInstanceId=Target.InstanceId;C.Enabled=A->Kind==E::Lock;break;
        case E::Favorite:case E::Unfavorite:C.Type=EAetherCommandType::SetItemFavorite;C.ItemInstanceId=Target.InstanceId;C.Enabled=A->Kind==E::Favorite;break;
        case E::Learn:C.Type=Current.PermanentSkillRank>0?EAetherCommandType::UpgradeSkill:EAetherCommandType::LearnSkill;C.SkillId=Target.DefinitionId;break;
        case E::BindHotbar:C.Type=EAetherCommandType::BindSkill;C.SkillId=Target.DefinitionId;C.SlotId=A->Argument;break;
        default:return Fail(TEXT("动作未连接到统一命令协议。"));
        }
        if(!AetherCommands::Encode(C,Result.CommandBytes,Reason))return false;
        Result.Command=C;
    }
    Out=MoveTemp(Result);Draft.Reset();
    if(Out.Command.IsSet()){Pending=Out;PendingContext=S.Context;Receipt.Reset();MarkPending();}
    return true;
}
void FAetherInspectionSession::MarkPending()
{
    // 不乐观修改数量、永久等级或装备；每次刷新/重新打开都保持同一待确认屏障。
    if(!Pending.IsSet()||!Details.IsSet()||!Details->CanInteract())return;
    Details->Message=TEXT("请求待确认，等待命令服务回执及提交快照。");
    for(auto& A:Details->Actions){A.bEnabled=false;A.DisabledReason=Details->Message;}
}
bool FAetherInspectionSession::RejectBeforeSend(FGuid Id)
{
    if(!Pending.IsSet()||!Pending->Command.IsSet()||Pending->Command->CommandId!=Id||Receipt.IsSet())return false;
    Pending.Reset();return true;
}
bool FAetherInspectionSession::Acknowledge(const FAetherCommandResult& Result)
{
    FString Reason;
    if(!Pending.IsSet()||!Pending->Command.IsSet()||Pending->Command->CommandId!=Result.CommandId||
        !AetherCommands::ValidateResult(Result,Reason))return false;
    // 存储不可用/忙碌可能需要查询原回执；保留原字节交给命令服务重试，不生成新命令。
    if(Result.Code==EAetherCommandCode::StorageUnavailable||Result.Code==EAetherCommandCode::Busy)return true;
    if(Result.Code==EAetherCommandCode::Applied||Result.Code==EAetherCommandCode::Replayed)Receipt=Result;
    else {Pending.Reset();Receipt.Reset();}
    return true;
}
FVector2D FAetherInspectionSession::PlacePopup(FVector2D Anchor,FVector2D Desired,FVector2D Viewport,float Margin)
{
    if(Anchor.ContainsNaN()||Desired.ContainsNaN()||Viewport.ContainsNaN()||!FMath::IsFinite(Margin))return FVector2D::ZeroVector;
    const double W=FMath::Max(0.,double(Viewport.X)),H=FMath::Max(0.,double(Viewport.Y));
    const double M=FMath::Clamp(double(Margin),0.,FMath::Min(W,H)*.5);
    const double X=FMath::Max(0.,double(Desired.X)),Y=FMath::Max(0.,double(Desired.Y));
    if(Anchor.X+X>W-M)Anchor.X-=X;
    if(Anchor.Y+Y>H-M)Anchor.Y-=Y;
    return FVector2D(FMath::Clamp(double(Anchor.X),M,FMath::Max(M,W-M-X)),FMath::Clamp(double(Anchor.Y),M,FMath::Max(M,H-M-Y)));
}
