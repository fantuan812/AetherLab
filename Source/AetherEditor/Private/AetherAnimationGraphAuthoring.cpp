#include "AetherAnimationAuthoring.h"
#include "AnimGraphNode_AetherCharacterPose.h"
#include "AnimGraphNode_AetherGeneratedPose.h"
#include "AnimGraphNode_Root.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AetherAnimation.h"
#include "AetherMotionSourceAnimInstance.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphSchema.h"
#include "Kismet2/BlueprintEditorUtils.h"

bool UAetherAnimationAuthoring::ConnectNativePose(UAnimBlueprint* Blueprint,bool SourcePose,FString& Reason)
{
 Reason=TEXT("Missing or incompatible animation blueprint");
 UClass* Parent=SourcePose?UAetherMotionSourceAnimInstance::StaticClass():UAetherAnimInstance::StaticClass();
 if(!Blueprint||!Blueprint->ParentClass||!Blueprint->ParentClass->IsChildOf(Parent))return false;
 UEdGraph* Graph=nullptr;UAnimGraphNode_Root* Root=nullptr;
 for(UEdGraph* Candidate:Blueprint->FunctionGraphs)
  if(Candidate&&Candidate->GetFName()==TEXT("AnimGraph"))
   for(UEdGraphNode* Node:Candidate->Nodes)if(auto* Output=Cast<UAnimGraphNode_Root>(Node)){Graph=Candidate;Root=Output;break;}
 if(!Graph||!Root){Reason=TEXT("Missing AnimGraph Output Pose");return false;}
 UClass* NodeClass=SourcePose?UAnimGraphNode_AetherGeneratedPose::StaticClass():UAnimGraphNode_AetherCharacterPose::StaticClass();
 UEdGraphNode* Native=nullptr;
 for(UEdGraphNode* Node:Graph->Nodes)if(Node&&Node->GetClass()==NodeClass){Native=Node;break;}
 Blueprint->Modify();Graph->Modify();Root->Modify();
 if(!Native)
 {
  Native=NewObject<UEdGraphNode>(Graph,NodeClass,NAME_None,RF_Transactional);
  Graph->AddNode(Native,false,false);Native->CreateNewGuid();Native->PostPlacedNewNode();Native->AllocateDefaultPins();
 }
 Native->NodePosX=Root->NodePosX-350;Native->NodePosY=Root->NodePosY;
 UEdGraphPin* Out=nullptr;UEdGraphPin* In=nullptr;
 for(auto* Pin:Native->Pins)if(Pin->Direction==EGPD_Output){Out=Pin;break;}
 for(auto* Pin:Root->Pins)if(Pin->Direction==EGPD_Input&&Pin->PinName==TEXT("Result")){In=Pin;break;}
 if(!Out||!In){Reason=TEXT("Pose pins missing");return false;}
 if(!Out->LinkedTo.Contains(In))
 {
  Graph->GetSchema()->BreakPinLinks(*In,true);
  if(!Graph->GetSchema()->TryCreateConnection(Out,In)){Reason=TEXT("Unable to connect native pose");return false;}
 }
 FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
 Reason.Reset();return true;
}
