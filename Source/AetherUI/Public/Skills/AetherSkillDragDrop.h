#pragma once
#include "Input/DragAndDrop.h"
#include "Inspection/AetherInspectionService.h"

// Slate 图形节点与快捷格共享固定快照身份；拖动过程中不根据当前选择重建。
class FAetherSkillDragDrop : public FDragDropOperation
{
public:
    DRAG_DROP_OPERATOR_TYPE(FAetherSkillDragDrop,FDragDropOperation)
    FAetherInspectRequest Request;
    static TSharedRef<FAetherSkillDragDrop> New(FAetherInspectRequest In)
    {auto Op=MakeShared<FAetherSkillDragDrop>();Op->Request=MoveTemp(In);Op->Construct();return Op;}
};
