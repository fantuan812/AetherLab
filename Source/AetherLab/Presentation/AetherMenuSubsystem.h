#pragma once
#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "AetherMenuSubsystem.generated.h"

class AAetherFrontierCharacter;

// 数值保持旧快捷键页码兼容；状态属于 LocalPlayer，不再由某一个 Widget 决定。
UENUM(BlueprintType)
enum class EAetherMenuPage : uint8 { None, Inventory, Journal, Skills, Map, Party, System };

USTRUCT(BlueprintType)
struct FAetherMenuPageMemory
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadWrite) FString Search;
    UPROPERTY(BlueprintReadWrite) float ScrollOffset=0;
    UPROPERTY(BlueprintReadWrite) FGuid SelectedInstance;
};

DECLARE_MULTICAST_DELEGATE(FOnAetherMenuChanged);

UCLASS()
class AETHERLAB_API UAetherMenuSubsystem : public ULocalPlayerSubsystem
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable) void TogglePage(EAetherMenuPage Page);
    UFUNCTION(BlueprintCallable) void OpenPage(EAetherMenuPage Page);
    UFUNCTION(BlueprintCallable) void Back();
    UFUNCTION(BlueprintCallable) void Close();
    // 异步弹窗只能关闭自己的顶层令牌，旧回调不能误关后来打开的弹窗。
    UFUNCTION(BlueprintCallable) FGuid PushLayer(FName Layer);
    UFUNCTION(BlueprintCallable) bool DismissLayer(FGuid Token);
    UFUNCTION(BlueprintPure) EAetherMenuPage GetPage() const {return CurrentPage;}
    UFUNCTION(BlueprintPure) bool IsOpen() const {return CurrentPage!=EAetherMenuPage::None;}
    UFUNCTION(BlueprintPure) int32 GetLayerCount() const {return Layers.Num();}
    UFUNCTION(BlueprintCallable) void SavePageMemory(EAetherMenuPage Page,const FAetherMenuPageMemory& Memory);
    UFUNCTION(BlueprintPure) FAetherMenuPageMemory GetPageMemory(EAetherMenuPage Page) const;
    void AttachPawn(AAetherFrontierCharacter* Pawn);
    AAetherFrontierCharacter* GetBoundPawn() const {return BoundPawn.Get();}
    virtual void Deinitialize() override;
    FOnAetherMenuChanged OnChanged;
private:
    void SetPage(EAetherMenuPage Page);
    void Publish(bool WasOpen);
    static bool ValidPage(EAetherMenuPage Page);
    EAetherMenuPage CurrentPage=EAetherMenuPage::None;
    struct FLayer {FGuid Token; FName Name;};
    TArray<FLayer> Layers;
    TMap<EAetherMenuPage,FAetherMenuPageMemory> PageMemory;
    TWeakObjectPtr<AAetherFrontierCharacter> BoundPawn;
};
