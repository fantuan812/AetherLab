#include "Interaction/AetherDialoguePresentation.h"
void FAetherDialoguePlayback::Reset(){Durations.Reset();State=EAetherDialoguePlaybackPhase::Closed;Index=INDEX_NONE;Elapsed=0;bAdvance=bSkip=false;}
bool FAetherDialoguePlayback::Start(const TArray<FAetherDialogueLine>& Lines,bool AllowAdvance,bool AllowSkip)
{
    Reset();if(Lines.IsEmpty()||Lines.Num()>16)return false;double Total=0;
    for(const auto& Line:Lines)
    {
        if(Line.Text.IsEmpty()||!FMath::IsFinite(Line.DurationSeconds)||Line.DurationSeconds<.05||Line.DurationSeconds>30){Reset();return false;}
        Total+=Line.DurationSeconds;Durations.Add(Line.DurationSeconds);
    }
    if(Total>180){Reset();return false;}
    State=EAetherDialoguePlaybackPhase::Speaking;Index=0;bAdvance=AllowAdvance;bSkip=AllowSkip;return true;
}
void FAetherDialoguePlayback::Next()
{
    Elapsed=0;if(++Index>=Durations.Num()){Index=INDEX_NONE;State=EAetherDialoguePlaybackPhase::Choices;}
}
bool FAetherDialoguePlayback::Tick(double DeltaSeconds)
{
    if(State!=EAetherDialoguePlaybackPhase::Speaking||!FMath::IsFinite(DeltaSeconds)||DeltaSeconds<=0)return false;
    Elapsed+=DeltaSeconds;bool Changed=false;
    while(State==EAetherDialoguePlaybackPhase::Speaking&&Elapsed>=Durations[Index])
    {const double Remaining=Elapsed-Durations[Index];Next();Elapsed=Remaining;Changed=true;}
    return Changed;
}
bool FAetherDialoguePlayback::Advance(){if(!bAdvance||State!=EAetherDialoguePlaybackPhase::Speaking)return false;Next();return true;}
bool FAetherDialoguePlayback::Skip(){if(!bSkip||State!=EAetherDialoguePlaybackPhase::Speaking)return false;Index=INDEX_NONE;Elapsed=0;State=EAetherDialoguePlaybackPhase::Choices;return true;}
