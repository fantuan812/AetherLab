#include "Inventory/AetherConsumableEffect.h"
namespace
{
bool Id(const FString& S)
{
    if(S.IsEmpty()||S.Len()>96)return false;
    for(TCHAR C:S)if(!((C>='a'&&C<='z')||(C>='A'&&C<='Z')||(C>='0'&&C<='9')||C=='_'||C=='-'||C=='.'))return false;
    return true;
}
bool Valid(const FAetherConsumableEffectV10& E)
{
    const auto& B=E.Before;const auto& A=E.After;
    return E.DeliveryId.IsValid()&&E.ItemInstanceId.IsValid()&&Id(E.DefinitionId)&&E.ProfileRevision>0&&E.ProfileRevision<MAX_int64&&
        (uint64(E.DeliveryId.A)<<32|E.DeliveryId.B)==uint64(E.ProfileRevision)&&(E.DeliveryId.C||E.DeliveryId.D)&&
        B.Validate()&&A.Validate()&&B.Health>0&&B.Revision<MAX_int64-1&&A.LifeId==B.LifeId&&A.Revision==B.Revision+1&&
        A.MaxHealth==B.MaxHealth&&A.MaxMana==B.MaxMana&&A.MaxStamina==B.MaxStamina&&
        A.Health>=B.Health&&A.Mana>=B.Mana&&A.Stamina>=B.Stamina&&
        (A.Health>B.Health||A.Mana>B.Mana||A.Stamina>B.Stamina)&&A.UseReadyAtUnixMs>=B.UseReadyAtUnixMs;
}
struct FWire
{
    TArray<uint8> Bytes;int32 Offset=0;bool Reading=false,OK=true;
    void UInt(uint64& V,int32 N)
    {
        if(!Reading){for(int32 I=0;I<N;++I)Bytes.Add(uint8(V>>(8*I)));return;}
        if(!OK||N>Bytes.Num()-Offset){OK=false;V=0;return;}
        V=0;for(int32 I=0;I<N;++I)V|=uint64(Bytes[Offset++])<<(8*I);
    }
    void Guid(FGuid& G)
    {
        uint64 A=G.A,B=G.B,C=G.C,D=G.D;UInt(A,4);UInt(B,4);UInt(C,4);UInt(D,4);
        if(Reading)G=FGuid(uint32(A),uint32(B),uint32(C),uint32(D));
    }
    void Long(int64& V){uint64 U=uint64(V);UInt(U,8);if(Reading){if(U>=uint64(MAX_int64)){OK=false;V=0;}else V=int64(U);}}
    void Number(double& D){uint64 U=0;FMemory::Memcpy(&U,&D,8);UInt(U,8);if(Reading)FMemory::Memcpy(&D,&U,8);}
    void State(FAetherResourceStateV10& S)
    {
        Guid(S.LifeId);Long(S.Revision);Long(S.UseReadyAtUnixMs);
        Number(S.Health);Number(S.Mana);Number(S.Stamina);Number(S.MaxHealth);Number(S.MaxMana);Number(S.MaxStamina);
    }
    void Effect(FAetherConsumableEffectV10& E)
    {
        uint64 Magic=0x46454341,Version=1;UInt(Magic,4);UInt(Version,2);
        if(Magic!=0x46454341||Version!=1){OK=false;return;}
        Guid(E.DeliveryId);Guid(E.ItemInstanceId);Long(E.ProfileRevision);
        uint64 Size=E.DefinitionId.Len();UInt(Size,1);if(Size>96){OK=false;return;}
        if(Reading)E.DefinitionId.Reset();
        for(int32 I=0;I<int32(Size)&&OK;++I){uint64 C=Reading?0:uint64(E.DefinitionId[I]);UInt(C,1);if(Reading)E.DefinitionId.AppendChar(TCHAR(C));}
        State(E.Before);State(E.After);
    }
};
bool Unpack(const FAetherEffectDelivery& D,const FString& Actor,FAetherConsumableEffectV10& E)
{return !Actor.IsEmpty()&&D.ActorId==Actor&&D.SchemaVersion==1&&AetherConsumableEffects::Decode(D.Payload,E)&&D.Id==E.DeliveryId;}
}
bool FAetherResourceStateV10::Validate() const
{
    const auto Resource=[](double V,double Max){return FMath::IsFinite(V)&&FMath::IsFinite(Max)&&Max>0&&Max<=1000000&&V>=0&&V<=Max;};
    return LifeId.IsValid()&&Revision>=0&&Revision<MAX_int64&&UseReadyAtUnixMs>=0&&UseReadyAtUnixMs<=253402300799999LL&&
        Resource(Health,MaxHealth)&&Resource(Mana,MaxMana)&&Resource(Stamina,MaxStamina);
}
bool FAetherResourceStateV10::Same(const FAetherResourceStateV10& O) const
{
    return LifeId==O.LifeId&&Revision==O.Revision&&UseReadyAtUnixMs==O.UseReadyAtUnixMs&&Health==O.Health&&Mana==O.Mana&&
        Stamina==O.Stamina&&MaxHealth==O.MaxHealth&&MaxMana==O.MaxMana&&MaxStamina==O.MaxStamina;
}
bool AetherConsumableEffects::Encode(const FAetherConsumableEffectV10& E,TArray<uint8>& Bytes)
{
    Bytes.Reset();if(!Valid(E))return false;FWire W;auto Copy=E;W.Effect(Copy);if(!W.OK)return false;Bytes=MoveTemp(W.Bytes);return true;
}
bool AetherConsumableEffects::Decode(const TArray<uint8>& Bytes,FAetherConsumableEffectV10& E)
{
    if(Bytes.Num()>512||Bytes.IsEmpty())return false;
    FWire R;R.Bytes=Bytes;R.Reading=true;FAetherConsumableEffectV10 Copy;R.Effect(Copy);
    if(!R.OK||R.Offset!=Bytes.Num()||!Valid(Copy))return false;E=MoveTemp(Copy);return true;
}
FAetherConsumableReceiver::FAetherConsumableReceiver(FString Actor,FAetherResourceStateV10 Initial)
    :Owner(MoveTemp(Actor)),Current(MoveTemp(Initial)){}
bool FAetherConsumableReceiver::Reserve(FGuid Id,FAetherResourceStateV10& Before)
{
    if(Owner.IsEmpty()||!Current.Validate()||Current.Health<=0||!Id.IsValid()||Reserved.IsValid()||Applied.Contains(Id)||Applied.Num()>=128)return false;
    Reserved=Id;Before=Current;return true;
}
bool FAetherConsumableReceiver::CancelUncommitted(FGuid Id)
{if(!IsReserved(Id))return false;Reserved.Invalidate();return true;}
bool FAetherConsumableReceiver::UpdateResources(const FAetherResourceStateV10& Next)
{
    if(Reserved.IsValid()||!Current.Validate()||!Next.Validate()||Current.Revision>=MAX_int64-1||
        Next.LifeId!=Current.LifeId||Next.Revision!=Current.Revision+1||Next.UseReadyAtUnixMs!=Current.UseReadyAtUnixMs)return false;
    Current=Next;return true;
}
EAetherEffectApplyCode FAetherConsumableReceiver::Apply(const FAetherEffectDelivery& D,const FString& Actor)
{
    using C=EAetherEffectApplyCode;FAetherConsumableEffectV10 E;
    if(Actor!=Owner||!Unpack(D,Actor,E))return C::Invalid;
    // 先查实际投递内容；重试发生在后续受伤之后，也绝不能再次把生命改回旧目标值。
    if(const auto* Bytes=Applied.Find(D.Id))return *Bytes==D.Payload?C::Replayed:C::Conflict;
    if(Applied.Num()>=128)return C::Capacity;
    if((Reserved.IsValid()&&Reserved!=D.Id)||!Current.Same(E.Before))return C::Conflict;
    Current=E.After;Applied.Add(D.Id,D.Payload);Reserved.Invalidate();return C::Applied;
}
bool FAetherConsumableReceiver::ForgetAcknowledged(FGuid Id){return Applied.Remove(Id)>0;}
TArray<FGuid> FAetherConsumableReceiver::PendingAcknowledgementIds() const
{TArray<FGuid> Ids;Applied.GetKeys(Ids);return Ids;}
bool FAetherConsumableReceiver::RecoverAtFullRespawn(const TArray<FAetherEffectDelivery>& Pending,const FString& Actor)
{
    if(Actor!=Owner||Actor.IsEmpty()||Reserved.IsValid()||!Applied.IsEmpty()||!Current.Validate()||Current.Revision!=0||Pending.Num()>128||
        Current.Health!=Current.MaxHealth||Current.Mana!=Current.MaxMana||Current.Stamina!=Current.MaxStamina)return false;
    TMap<FGuid,TArray<uint8>> Recovered;int64 Cooldown=Current.UseReadyAtUnixMs;
    for(const auto& D:Pending)
    {
        FAetherConsumableEffectV10 E;
        if(!Unpack(D,Actor,E)||E.Before.LifeId==Current.LifeId||Recovered.Contains(D.Id))return false;
        Recovered.Add(D.Id,D.Payload);Cooldown=FMath::Max(Cooldown,E.After.UseReadyAtUnixMs);
    }
    // 全资源重生本身已覆盖旧生命的恢复需求；保留待确认 ID 和最晚冷却，不向新生命再加一次治疗。
    Applied=MoveTemp(Recovered);Current.UseReadyAtUnixMs=Cooldown;return true;
}
