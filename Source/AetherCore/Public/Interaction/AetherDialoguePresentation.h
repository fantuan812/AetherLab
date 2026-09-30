#pragma once
#include "CoreMinimal.h"
struct FAetherDialogueLine {FString Text;double DurationSeconds=0;};
struct FAetherDialogueCameraDefinition
{
    FVector Offset=FVector::ZeroVector,LookAtOffset=FVector::ZeroVector;
    double Fov=0,BlendInSeconds=0,BlendOutSeconds=0,ProbeRadiusCm=0;
};
struct FAetherDialoguePresentation
{
    FString Id,AdvanceLabel,SkipLabel;
    bool bAllowAdvance=false,bAllowSkip=false;
    TOptional<FAetherDialogueCameraDefinition> Camera;
};
enum class EAetherDialoguePlaybackPhase:uint8 {Closed,Speaking,Choices};
// Deterministic local display clock. It has no command, quest, actor or persistence access.
struct AETHERCORE_API FAetherDialoguePlayback
{
    bool Start(const TArray<FAetherDialogueLine>& Lines,bool AllowAdvance,bool AllowSkip);
    bool Tick(double DeltaSeconds);
    bool Advance();
    bool Skip();
    void Reset();
    EAetherDialoguePlaybackPhase Phase() const{return State;}
    int32 LineIndex() const{return Index;}
private:
    TArray<double> Durations;
    EAetherDialoguePlaybackPhase State=EAetherDialoguePlaybackPhase::Closed;
    int32 Index=INDEX_NONE;
    double Elapsed=0;
    bool bAdvance=false,bSkip=false;
    void Next();
};
