#include "Framework/AetherFrontier.h"
#include "Inventory/AetherResourceGate.h"
#include "EngineUtils.h"
int32 AAetherFrontierMode::PartySize(const FString& Leader) const
{
    int32 Count=0;
    for(TActorIterator<AAetherPlayerState> It(GetWorld());It;++It)if(!It->IsInactive()&&It->PartyLeader==Leader)++Count;
    for(const auto& C:Companions)if(IsValid(C)&&C->CompanionOwner&&C->CompanionOwner->ProfileState()&&C->CompanionOwner->ProfileState()->PartyLeader==Leader)++Count;
    return Count;
}
void AAetherFrontierCharacter::ServerPartyAction_Implementation(FName Action,AAetherPlayerState* Target,AAetherFrontierCharacter* Buddy)
{
    if(Action!="Invite"&&Action!="AcceptInvite"&&Action!="DeclineInvite"&&Action!="LeaveParty"&&Action!="Dismiss"&&Action!="PartyCommand")return;
    if(!HasAuthority()||!Alive()||bTravelPending||ResourceGate->IsBlocked()||CombatTime()<NextServerAction)return;
    NextServerAction=CombatTime()+.2f;
    if((Target&&(!IsValid(Target)||Target->GetWorld()!=GetWorld()))||(Buddy&&(!IsValid(Buddy)||Buddy->GetWorld()!=GetWorld())))return;
    if(auto* M=GetWorld()->GetAuthGameMode<AAetherFrontierMode>())M->ExecutePartyAction(this,Action,Target,Buddy);
}
bool AAetherFrontierMode::ExecutePartyAction(AAetherFrontierCharacter* C,FName Action,AAetherPlayerState* InviteTarget,AAetherFrontierCharacter* Companion)
{
    auto* PS=C?C->ProfileState():nullptr;if(!PS)return false;
    if(Action!="Invite"&&Action!="AcceptInvite"&&Action!="DeclineInvite"&&Action!="LeaveParty"&&Action!="Dismiss"&&Action!="PartyCommand"&&Action!="Recruit")return false;
    if(Action=="DeclineInvite"){PS->InvitedBy.Empty();PS->InvitationExpires=0;PS->ForceNetUpdate();return true;}
    if(!CanChangeParty(C)&&Action!="PartyCommand"){C->Notify(TEXT("请在城镇安全区域且未参与遭遇时调整队伍。"));return true;}
    if(Action=="Invite")
    {
        if(PS->PartyLeader!=PS->Profile.CharacterId||PartySize(PS->PartyLeader)>=4){C->Notify(TEXT("只有有空位的队长可以邀请。"));return true;}
        // 正式 UI 固定所选 PlayerState；旧键盘快捷键保留最近目标选择。
        auto* Target=InviteTarget;
        if(!Target)
        {
            double Best=FMath::Square(500.);
            for(TActorIterator<AAetherFrontierCharacter> It(GetWorld());It;++It)if(*It!=C&&It->ProfileState()&&It->ProfileState()->PartyLeader!=PS->PartyLeader&&CanChangeParty(*It))
            {const double D=FVector::DistSquared(C->GetActorLocation(),It->GetActorLocation());if(D<Best){Best=D;Target=It->ProfileState();}}
        }
        auto* Pawn=Target?Cast<AAetherFrontierCharacter>(Target->GetPawn()):nullptr;
        if(!Pawn||Target==PS||Target->PartyLeader==PS->PartyLeader||Target->IsInactive()||!CanChangeParty(Pawn)||
           FVector::DistSquared(C->GetActorLocation(),Pawn->GetActorLocation())>FMath::Square(500.))
        {C->Notify(TEXT("所选玩家已离开邀请范围或状态发生变化。"));return true;}
        Target->InvitedBy=PS->Profile.CharacterId;Target->InvitationExpires=C->CombatTime()+30;Target->ForceNetUpdate();
        C->Notify(TEXT("邀请已发送，三十秒内可在队伍页接受或拒绝。"));return true;
    }
    if(Action=="AcceptInvite")
    {
        if(PS->InvitedBy.IsEmpty()||C->CombatTime()>PS->InvitationExpires){PS->InvitedBy.Empty();C->Notify(TEXT("邀请已过期。"));return true;}
        AAetherPlayerState* Leader=nullptr;
        for(TActorIterator<AAetherPlayerState> It(GetWorld());It;++It)if(!It->IsInactive()&&It->Profile.CharacterId==PS->InvitedBy&&It->PartyLeader==It->Profile.CharacterId)Leader=*It;
        int32 Joining=1;for(const auto& B:Companions)if(IsValid(B)&&B->CompanionOwner==C)++Joining;
        if(!Leader||!CanChangeParty(Cast<AAetherFrontierCharacter>(Leader->GetPawn()))||PartySize(Leader->PartyLeader)+Joining>4)
        {C->Notify(TEXT("邀请方已失效或队伍没有足够空位。"));return true;}
        const FString Id=Leader->PartyLeader;LeaveParty(PS);PS->PartyLeader=Id;PS->InvitedBy.Empty();PS->InvitationExpires=0;PS->ForceNetUpdate();return true;
    }
    if(Action=="LeaveParty"){LeaveParty(PS);PS->ForceNetUpdate();return true;}
    if(Action=="PartyCommand"||Action=="Dismiss")
    {
        if(Action=="PartyCommand"&&Encounters&&Encounters->Abbey.Phase==EAetherEncounterPhase::Channel)
        {
            auto* Valve=Prop("AbbeyValve");if(Valve&&FVector::DistSquared(C->GetActorLocation(),Valve->GetActorLocation())<FMath::Square(300.))
            {C->Notify(Encounters->Channel(C,true));return true;}
        }
        for(const auto& B:Companions)if(IsValid(B)&&B->CompanionOwner==C&&(!Companion||Companion==B))
        {if(Action=="Dismiss")B->Destroy();else{B->bCompanionHold=!B->bCompanionHold;B->ForceNetUpdate();}}
        return true;
    }
    if(Action=="Recruit"){C->Notify(RecruitCompanion(C));return true;}return false;
}
