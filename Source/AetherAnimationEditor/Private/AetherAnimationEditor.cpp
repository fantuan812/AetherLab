#include "Modules/ModuleManager.h"
#include "UObject/CoreRedirects.h"

class FAetherAnimationEditorModule : public IModuleInterface
{
 virtual void StartupModule() override
 {
  // 升级早期作者资产中的节点所属模块，不依赖个人配置或运行时模块。
  TArray<FCoreRedirect> Redirects;
  Redirects.Emplace(ECoreRedirectFlags::Type_Class,TEXT("/Script/AetherEditor.AnimGraphNode_AetherCharacterPose"),TEXT("/Script/AetherAnimationEditor.AnimGraphNode_AetherCharacterPose"));
  FCoreRedirects::AddRedirectList(Redirects,TEXT("AetherAnimationEditor"));
 }
};
IMPLEMENT_MODULE(FAetherAnimationEditorModule,AetherAnimationEditor)
