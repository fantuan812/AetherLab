#pragma once
#include "CoreMinimal.h"
#include <motionbricks/inference.h>

// 只有固定宽度的 C ABI 穿过库边界；不链接/传递上游 C++ 对象或异常。
class FMotionBricksApi
{
public:
    static constexpr const TCHAR* Revision=TEXT("ee0cf5d9035f639ed0787f390fb1ce05d6a4c463");
    ~FMotionBricksApi();
    bool Load(FString& Reason);
    FString RuntimeRoot;
    TSet<FString> VerifiedFiles;
    FString StagedBackend;
#define MB_FUNCTION(Name) decltype(&::Name) Name=nullptr;
#include "MotionBricksRequired.inl"
#undef MB_FUNCTION
private:
    bool bLoaded=false;
    void* Library=nullptr;
    TArray<void*> Dependencies;
    bool VerifyStage(FString& Reason);
};
