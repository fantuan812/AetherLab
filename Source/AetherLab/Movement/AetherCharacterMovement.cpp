#include "Movement/AetherCharacterMovement.h"
#include "Characters/AetherFrontierCharacter.h"

namespace
{
class FAetherClientPrediction final : public FNetworkPredictionData_Client_Character
{
public:
    explicit FAetherClientPrediction(const UCharacterMovementComponent& Movement):FNetworkPredictionData_Client_Character(Movement){}
    virtual FSavedMovePtr AllocateNewMove() override {return FSavedMovePtr(new FAetherSavedMove());}
};
}
UAetherCharacterMovement::UAetherCharacterMovement()
{
    GetNavAgentPropertiesRef().bCanCrouch=true;
    SetCrouchedHalfHeight(48);
    MaxWalkSpeedCrouched=CrouchSpeed;
}
bool UAetherCharacterMovement::CanSprint() const
{
    const auto* C=Cast<AAetherFrontierCharacter>(CharacterOwner);
    return C&&C->CanStartLocomotion()&&!C->IsCrouched()&&!bWantsToCrouch&&IsMovingOnGround()&&C->Stamina()>5;
}
float UAetherCharacterMovement::GetMaxSpeed() const
{
    const auto* C=Cast<AAetherFrontierCharacter>(CharacterOwner);
    if(!C)return Super::GetMaxSpeed();
    if(!C->Alive()||C->bTravelPending||C->CombatTime()<C->StunUntil)return 0;
    if(MovementMode!=MOVE_Walking&&MovementMode!=MOVE_NavWalking)return Super::GetMaxSpeed();
    // 唯一的行走速度决策点，基类战斗 Tick 的旧 MaxWalkSpeed 不再覆盖玩家姿态。
    float Speed=C->Fighter==EAetherFighter::Player?WalkSpeed:C->Fighter==EAetherFighter::Wolf?380.f:230.f;
    const bool SprintIntent=CharacterOwner->GetLocalRole()==ROLE_SimulatedProxy?C->bSprinting:bWantsSprint;
    if(C->IsCrouched())Speed=CrouchSpeed;
    else if(SprintIntent&&CanSprint())Speed=SprintSpeed;
    if(C->bBlocking)Speed=FMath::Min(Speed,220.f);
    return Speed*(C->Reactive->State.IceFraction>.5?.5f:1.f);
}
bool UAetherCharacterMovement::CanCrouchInCurrentState() const
{
    const auto* C=Cast<AAetherFrontierCharacter>(CharacterOwner);
    return Super::CanCrouchInCurrentState()&&C&&(C->IsCrouched()||C->CanStartLocomotion())&&IsMovingOnGround();
}
bool UAetherCharacterMovement::TryStand()
{
    bWantsToCrouch=false;
    // UnCrouch 保留引擎向上净空检查，失败时真实胶囊仍维持下蹲。
    if(CharacterOwner&&CharacterOwner->IsCrouched())UnCrouch(false);
    return CharacterOwner&&!CharacterOwner->IsCrouched();
}
void UAetherCharacterMovement::UpdateFromCompressedFlags(uint8 Flags)
{
    Super::UpdateFromCompressedFlags(Flags);
    bWantsSprint=(Flags&FSavedMove_Character::FLAG_Custom_0)!=0;
    // MoveAutonomous 在姿态更新前检查跳跃；服务端先做同一净空检查，
    // 避免客户端已起身而服务端仍蹲着，导致短按跳跃被错误纠正。
    const auto* C=Cast<AAetherFrontierCharacter>(CharacterOwner);
    if(C&&C->CanStartLocomotion()&&!bWantsToCrouch&&
        ((Flags&FSavedMove_Character::FLAG_JumpPressed)!=0||bWantsSprint))TryStand();
}
void UAetherCharacterMovement::UpdateCharacterStateBeforeMovement(float Delta)
{
    Super::UpdateCharacterStateBeforeMovement(Delta);
    if(auto* C=Cast<AAetherFrontierCharacter>(CharacterOwner))
        if(C->HasAuthority()||C->IsLocallyControlled())C->bSprinting=bWantsSprint&&CanSprint();
}
FNetworkPredictionData_Client* UAetherCharacterMovement::GetPredictionData_Client() const
{
    if(!ClientPredictionData)const_cast<UAetherCharacterMovement*>(this)->ClientPredictionData=new FAetherClientPrediction(*this);
    return ClientPredictionData;
}
void FAetherSavedMove::Clear(){Super::Clear();bSavedWantsSprint=false;}
uint8 FAetherSavedMove::GetCompressedFlags() const
{return Super::GetCompressedFlags()|(bSavedWantsSprint?FLAG_Custom_0:0);}
bool FAetherSavedMove::CanCombineWith(const FSavedMovePtr& Move,ACharacter* C,float Delta) const
{
    // 按下与松开不能合并，服务器必须看到准确的冲刺意图边沿。
    if(bSavedWantsSprint!=static_cast<const FAetherSavedMove*>(Move.Get())->bSavedWantsSprint)return false;
    return Super::CanCombineWith(Move,C,Delta);
}
void FAetherSavedMove::SetMoveFor(ACharacter* C,float Delta,const FVector& Accel,FNetworkPredictionData_Client_Character& Data)
{
    Super::SetMoveFor(C,Delta,Accel,Data);
    bSavedWantsSprint=CastChecked<UAetherCharacterMovement>(C->GetCharacterMovement())->bWantsSprint;
}
void FAetherSavedMove::PrepMoveFor(ACharacter* C)
{
    Super::PrepMoveFor(C);
    CastChecked<UAetherCharacterMovement>(C->GetCharacterMovement())->bWantsSprint=bSavedWantsSprint;
}
