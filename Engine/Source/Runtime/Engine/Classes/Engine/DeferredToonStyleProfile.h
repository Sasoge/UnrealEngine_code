#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DeferredToonStyleProfile.generated.h"

// [DeferredToon] Data-asset layer metadata uses stable numeric IDs so the
// registry can add or retire render targets without changing this asset ABI.
USTRUCT(BlueprintType)
struct ENGINE_API FDeferredToonStyleInputLayer
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Deferred Toon Layer", meta=(ClampMin="0", ClampMax="65535"))
    int32 InputId = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Deferred Toon Layer", meta=(ClampMin="0", ClampMax="5"))
    int32 Channel = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Deferred Toon Layer", meta=(ClampMin="0", ClampMax="2"))
    int32 Operation = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Deferred Toon Layer")
    bool bInvert = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Deferred Toon Layer", meta=(ClampMin="0.0", ClampMax="1.0", UIMin="0.0", UIMax="1.0"))
    float Weight = 1.0f;
};

// [DeferredToon] Profile assets are the author-facing list of semantic RT
// layers. The render proxy copies this bounded list into a fixed POD stack.
UCLASS(BlueprintType)
class ENGINE_API UDeferredToonStyleProfile : public UDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Deferred Toon Profile")
    TArray<FDeferredToonStyleInputLayer> Layers;
};
