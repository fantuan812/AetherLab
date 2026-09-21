#include "Definitions/AetherV10Definitions.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

const FAetherV10Definitions& FAetherV10Definitions::Get()
{
    static const FAetherV10Definitions Value=[]
    {
        FAetherV10Definitions D;D.Rules=FAetherRules::Get();D.Skills=FAetherSkillDefinitionsV10::Get();
        if(!D.Rules.bValid||!D.Skills.Validate(D.Error)){if(D.Error.IsEmpty())D.Error=D.Rules.Error;return D;}
        auto Read=[&](const TCHAR* Name,FString& Text)
        {
            if(FFileHelper::LoadFileToString(Text,*(FPaths::ProjectContentDir()/TEXT("AetherCore/Definitions/V10")/Name)))return true;
            D.Error=FString(TEXT("Missing v10 definition: "))+Name;return false;
        };
        FString Text;if(!Read(TEXT("Items.json"),Text))return D;
        D.Items=FAetherV10ItemDefinitions::Parse(Text,D.Error);if(!D.Items.Validate(D.Error))return D;
        for(const auto& Pair:D.Items.Items)for(const auto& Grant:Pair.Value.SkillGrants)
            if(!D.Skills.Effect(Grant.Key,Grant.Value)){D.Error=TEXT("Equipment references unknown skill rank");return D;}
        if(!Read(TEXT("Economy.json"),Text))return D;
        D.Economy=FAetherEconomyDefinitionsV10::Parse(Text,D.Items,D.Error);if(!D.Economy.Validate(D.Items,D.Error))return D;
        if(!Read(TEXT("Interactions.json"),Text))return D;
        D.Interactions=FAetherInteractionDefinitions::Parse(Text,D.Rules,D.Economy,D.Error);
        if(!D.Interactions.Validate(D.Rules,D.Economy,D.Error))return D;
        if(!Read(TEXT("Progression.json"),Text))return D;
        D.Progression=FAetherQuestProgressionDefinitions::Parse(Text,D.Rules,D.Error);
        D.bValid=D.Progression.Validate(D.Rules,D.Error);return D;
    }();return Value;
}
