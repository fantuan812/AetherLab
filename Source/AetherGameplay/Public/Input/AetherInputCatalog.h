#pragma once
#include "CoreMinimal.h"
#include "InputCoreTypes.h"
// 默认键位、设置名称与导出的操作说明共用此目录；保存的 ActionId 不改名。
namespace AetherInputCatalog
{
struct FEntry {FName Id;const TCHAR* Label;FKey Key;};
inline const TArray<FEntry>& Entries()
{
    static const TArray<FEntry> Values={
        {"Attack",TEXT("攻击：松开轻击／蓄力重击"),EKeys::LeftMouseButton},
        {"Guard",TEXT("格挡"),EKeys::RightMouseButton},
        {"Sprint",TEXT("冲刺"),EKeys::LeftShift},
        {"Jump",TEXT("跳跃／翻越"),EKeys::SpaceBar},
        {"Crouch",TEXT("下蹲"),EKeys::LeftControl},
        {"Dodge",TEXT("闪避"),EKeys::LeftAlt},
        {"Cast",TEXT("施放所选技能"),EKeys::MiddleMouseButton},
        {"One",TEXT("选择技能一"),EKeys::One},{"Two",TEXT("选择技能二"),EKeys::Two},
        {"Three",TEXT("选择技能三"),EKeys::Three},{"Four",TEXT("选择技能四"),EKeys::Four},
        {"Interact",TEXT("交互"),EKeys::E},{"Potion",TEXT("使用生命药剂"),EKeys::Q},
        {"Z",TEXT("使用法力药剂"),EKeys::Z},{"Carry",TEXT("搬起／放下"),EKeys::G},
        {"Push",TEXT("推动"),EKeys::V},{"Throw",TEXT("投掷搬运物件"),EKeys::C},
        {"Lock",TEXT("锁定目标"),EKeys::F},{"R",TEXT("装备所选物品"),EKeys::R},
        {"T",TEXT("切换盾牌"),EKeys::T},{"B",TEXT("拆分堆叠"),EKeys::B},
        {"N",TEXT("合并堆叠"),EKeys::N},{"Tab",TEXT("菜单内切换区域"),EKeys::Tab},
        {"Delete",TEXT("背包出售／队伍解散同伴"),EKeys::Delete},
        {"Seven",TEXT("购买法力药剂"),EKeys::Seven},{"Eight",TEXT("购买口粮"),EKeys::Eight},
        {"I",TEXT("背包"),EKeys::I},{"J",TEXT("任务日志"),EKeys::J},
        {"K",TEXT("技能"),EKeys::K},{"M",TEXT("地图"),EKeys::M},
        {"P",TEXT("队伍"),EKeys::P},{"Escape",TEXT("系统菜单／返回"),EKeys::Escape},
        {"Claim",TEXT("领取任务奖励"),EKeys::Enter},{"H",TEXT("同伴跟随／留守"),EKeys::H},
        {"Invite",TEXT("邀请组队"),EKeys::Y},{"AcceptInvite",TEXT("接受邀请"),EKeys::U},
        {"LeaveParty",TEXT("离开队伍"),EKeys::O},{"F8",TEXT("回据点恢复"),EKeys::F8},
        {"F5",TEXT("请求保存"),EKeys::F5},{"Debug",TEXT("诊断信息"),EKeys::F10},
        {"Weather",TEXT("开发天气控制"),EKeys::F11}
    };return Values;
}
inline const FEntry* Find(FName Id){return Entries().FindByPredicate([&](const auto& E){return E.Id==Id;});}
inline FString Label(FName Id){const auto* E=Find(Id);return E?FString(E->Label):Id.ToString();}
inline FKey DefaultKey(FName Id,FKey Fallback){const auto* E=Find(Id);return E?E->Key:Fallback;}
}
