#include "AetherFrontier.h"
#include "EngineUtils.h"
bool AAetherFrontierMode::ExecutePartyAction(AAetherFrontierCharacter* C,FName Action)
{
 auto* Mode=this;auto* PS=C?C->ProfileState():nullptr;if(!PS)return false;
    if((Action=="Invite"||Action=="AcceptInvite"||Action=="LeaveParty"||Action=="Dismiss")&&!Mode->CanChangeParty(C))
    {C->Notify(TEXT("Change party at town, out of combat and outside active encounters."));return true;}
    if(Action=="Invite")
    {
        if(PS->PartyLeader!=PS->Profile.CharacterId)return true;AAetherPlayerState* Target=nullptr;double Best=FMath::Square(500.);
        for(TActorIterator<AAetherFrontierCharacter> It(C->GetWorld());It;++It)if(*It!=C&&It->ProfileState()&&It->ProfileState()->PartyLeader!=PS->PartyLeader&&Mode->CanChangeParty(*It))
        {double D=FVector::DistSquared(C->GetActorLocation(),It->GetActorLocation());if(D<Best){Best=D;Target=It->ProfileState();}}
        if(Target){Target->InvitedBy=PS->Profile.CharacterId;Target->InvitationExpires=C->CombatTime()+30;C->Notify(TEXT("Party invitation sent; recipient P + U accepts within 30 seconds."));}return true;
    }
    if(Action=="AcceptInvite")
    {
        if(PS->InvitedBy.IsEmpty()||C->CombatTime()>PS->InvitationExpires)return true;
        for(TActorIterator<AAetherPlayerState> It(C->GetWorld());It;++It)if(It->Profile.CharacterId==PS->InvitedBy&&It->PartyLeader==It->Profile.CharacterId&&Mode->CanChangeParty(Cast<AAetherFrontierCharacter>(It->GetPawn())))
        {const FString Leader=It->PartyLeader;Mode->LeaveParty(PS);PS->PartyLeader=Leader;break;}
        PS->InvitedBy.Empty();return true;
    }
    if(Action=="LeaveParty"){Mode->LeaveParty(PS);return true;}
    if(Action=="PartyCommand"||Action=="Dismiss")
    {if(Action=="PartyCommand"&&Mode->Encounters&&Mode->Encounters->Abbey.Phase==EAetherEncounterPhase::Channel&&FVector::DistSquared(C->GetActorLocation(),Mode->Prop("AbbeyValve")->GetActorLocation())<FMath::Square(300.)){C->Notify(Mode->Encounters->Channel(C,true));return true;}for(const auto& B:Mode->Companions)if(IsValid(B)&&B->CompanionOwner==C){if(Action=="Dismiss")B->Destroy();else B->bCompanionHold=!B->bCompanionHold;}return true;}
    if(Action=="Recruit"){C->Notify(Mode->RecruitCompanion(C));return true;}
 return false;
}
