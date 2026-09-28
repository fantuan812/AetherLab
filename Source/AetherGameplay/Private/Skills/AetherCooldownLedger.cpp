#include "Skills/AetherCooldownLedger.h"
void UAetherCooldownLedger::Prune()
{
    const double Now=FPlatformTime::Seconds();
    for(auto It=Deadlines.CreateIterator();It;++It)
    {
        for(auto D=It.Value().CreateIterator();D;++D)if(D.Value()<=Now)D.RemoveCurrent();
        if(It.Value().IsEmpty())It.RemoveCurrent();
    }
}
TMap<FString,double> UAetherCooldownLedger::Read(const FString& Id)
{
    Prune();TMap<FString,double> Remaining;const double Now=FPlatformTime::Seconds();
    if(const auto* Row=Deadlines.Find(Id))for(const auto& D:*Row)Remaining.Add(D.Key,FMath::Max(0.,D.Value-Now));
    return Remaining;
}
void UAetherCooldownLedger::Put(const FString& Id,const FString& Key,double Seconds)
{
    Prune();if(Id.IsEmpty()||Id.Len()>32||Key.Len()>128||!FMath::IsFinite(Seconds)||Seconds<=0||Seconds>3600)return;
    auto& Row=Deadlines.FindOrAdd(Id);double& End=Row.FindOrAdd(Key);End=FMath::Max(End,FPlatformTime::Seconds()+Seconds);
}
