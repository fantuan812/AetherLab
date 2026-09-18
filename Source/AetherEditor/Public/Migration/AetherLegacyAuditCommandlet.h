#pragma once
#include "Commandlets/Commandlet.h"
#include "AetherLegacyAuditCommandlet.generated.h"

// 编辑器离线预检入口。这里只验证旧档，不创建/启用新数据库。
UCLASS()
class UAetherLegacyAuditCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    UAetherLegacyAuditCommandlet();
    virtual int32 Main(const FString& Params) override;
};
