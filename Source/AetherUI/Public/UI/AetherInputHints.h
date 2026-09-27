#pragma once
#include "Characters/AetherFrontierCharacter.h"
#include "CommonInputSubsystem.h"
#include "Engine/LocalPlayer.h"
namespace AetherInputHints
{
inline bool IsGamepad(const ULocalPlayer* Player)
{const auto* Input=Player?Player->GetSubsystem<UCommonInputSubsystem>():nullptr;return Input&&Input->GetCurrentInputType()==ECommonInputType::Gamepad;}
inline FString Label(const AAetherFrontierCharacter& Pawn,const ULocalPlayer* Player,FName Action)
{
    if(IsGamepad(Player))
    {
        static const TMap<FName,FString> Labels={{"Carry",TEXT("LB + ↑")},{"Throw",TEXT("LB + →")},{"Push",TEXT("LB + ↓")},
            {"Z",TEXT("LB + ←")},{"Crouch",TEXT("LB + B")},{"CrouchToggle",TEXT("LB + B")},
            {"One",TEXT("↑")},{"Two",TEXT("→")},{"Three",TEXT("↓")},{"Four",TEXT("←")},
            {"J",TEXT("View → RB 任务")},{"Interact",TEXT("X")},{"F8",TEXT("Y")},{"Cast",TEXT("RB")}};
        if(const auto* Value=Labels.Find(Action))return *Value;
        return TEXT("菜单");
    }
    return Pawn.BindingFor(Action).GetDisplayName().ToString();
}
}
