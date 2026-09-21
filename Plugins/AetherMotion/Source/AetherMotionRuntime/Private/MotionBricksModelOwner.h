#pragma once
#include "MotionBricksApi.h"
#include "AetherMotionTypes.h"

class FMotionBricksModelOwner
{
public:
    explicit FMotionBricksModelOwner(FMotionBricksApi& In):Api(In){}
    ~FMotionBricksModelOwner();
    bool Initialize(EAetherMotionBackend Backend,uint32 Threads,FString& Reason);
    FAetherMotionClipPtr Generate(const FAetherMotionInput& Input,FString& Reason);
    void RemoveAgent(uint64 Id);
private:
    struct FAgent
    {
        mb_agent* Handle=nullptr;
        FAetherMotionStamp Stamp;
        uint64 LastSequence=0;
        uint32 Advanced=0;
    };
    FMotionBricksApi& Api;
    mb_runtime_options* Options=nullptr;
    mb_model* Model=nullptr;
    TMap<FString,mb_style*> Styles;
    TMap<uint64,FAgent> Agents;
    TSharedPtr<FAetherMotionSkeleton,ESPMode::ThreadSafe> Skeleton;
    mb_style* Style(const FString& Id,FString& Reason);
    FAetherMotionClipPtr Copy(mb_motion* Native,const FAetherMotionInput& Input,FString& Reason);
};
