#include "Definitions/AetherV10Definitions.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Combat/AetherControlledActionDefinition.h"
#include "Effects/AetherBuffState.h"
#include "Skills/AetherNpcSkillDefinitions.h"

const FAetherV10Definitions& FAetherV10Definitions::Get()
{
    static const FAetherV10Definitions Value=[]
    {
        FAetherV10Definitions D;D.Rules=FAetherRules::Get();D.Skills=FAetherSkillDefinitionsV10::Get();
        if(!FAetherControlledActionCatalog::Get().bValid){D.Error=FAetherControlledActionCatalog::Get().Error;return D;}
        for(const auto& Action:AetherControlledActions::All())if(!Action.IsValid())
        {D.Error=TEXT("Invalid controlled action rule");return D;}
        if(!D.Rules.bValid||!D.Skills.Validate(D.Error)){if(D.Error.IsEmpty())D.Error=D.Rules.Error;return D;}
        if(!FAetherNpcSkillDefinitions::Get().bValid){D.Error=FAetherNpcSkillDefinitions::Get().Error;return D;}
        if(!FAetherBuffDefinitions::Get().bValid){D.Error=FAetherBuffDefinitions::Get().Error;return D;}
        for(const auto& Skill:D.Skills.Skills)for(const auto& Rank:Skill.Value.Ranks)
            if(!Rank.BuffId.IsEmpty()&&!FAetherBuffDefinitions::Get().Buffs.Contains(Rank.BuffId))
            {D.Error=TEXT("Skill references unknown buff executor result");return D;}
        for(const auto& Use:D.Rules.Uses)if(!Use.Value.BuffId.IsEmpty()&&!FAetherBuffDefinitions::Get().Buffs.Contains(Use.Value.BuffId))
        {D.Error=TEXT("Consumable references unknown buff");return D;}
        auto Read=[&](const TCHAR* Name,FString& Text)
        {
            if(FFileHelper::LoadFileToString(Text,*(FPaths::ProjectContentDir()/TEXT("AetherCore/Definitions/V10")/Name)))return true;
            D.Error=FString(TEXT("Missing v10 definition: "))+Name;return false;
        };
        FString Text;if(!Read(TEXT("Items.json"),Text))return D;
        D.Items=FAetherV10ItemDefinitions::Parse(Text,D.Error);if(!D.Items.Validate(D.Error))return D;
        for(const auto& Pair:D.Items.Items)
            if(!Pair.Value.UseId.IsEmpty()&&!D.Rules.Uses.Contains(FName(*Pair.Value.UseId)))
            {D.Error=TEXT("Item references an unknown use definition");return D;}
        for(const auto& Pair:D.Items.Items)for(const auto& Grant:Pair.Value.SkillGrants)
            if(!D.Skills.Effect(Grant.Key,Grant.Value)){D.Error=TEXT("Equipment references unknown skill rank");return D;}
        if(!Read(TEXT("Containers.json"),Text))return D;
        D.Containers=FAetherContainerDefinitions::Parse(Text);if(!D.Containers.bValid){D.Error=D.Containers.Error;return D;}
        if(!Read(TEXT("Economy.json"),Text))return D;
        D.Economy=FAetherEconomyDefinitionsV10::Parse(Text,D.Items,D.Error);if(!D.Economy.Validate(D.Items,D.Error))return D;
        if(!Read(TEXT("Interactions.json"),Text))return D;
        D.Interactions=FAetherInteractionDefinitions::Parse(Text,D.Rules,D.Economy,D.Error);
        if(!D.Interactions.Validate(D.Rules,D.Economy,D.Error))return D;
        for(const auto& Target:D.Interactions.Targets)for(const auto& Action:Target.Value.Actions)
            if((Action.Kind==EAetherInteractionActionKind::RecruitGuard||Action.Kind==EAetherInteractionActionKind::RecruitHealer)&&
                !FAetherNpcSkillDefinitions::Get().Find(Action.ServiceId))
            {D.Error=TEXT("Recruitment references an unknown NPC capability loadout");return D;}
        if(!Read(TEXT("Progression.json"),Text))return D;
        D.Progression=FAetherQuestProgressionDefinitions::Parse(Text,D.Rules,D.Error);
        D.bValid=D.Progression.Validate(D.Rules,D.Error);return D;
    }();return Value;
}
