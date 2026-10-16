#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DeferredToonStyleProfile.generated.h"

// [DeferredToon] These editor-facing enums keep the volume details panel
// self-documenting while preserving the same stable numeric IDs used by the
// render-thread registry and existing serialized profiles.
UENUM(BlueprintType)
enum class EDeferredToonStyleInputId : uint8
{
    Unknown = 0 UMETA(DisplayName="None"),
    DirectLight = 1 UMETA(DisplayName="Direct Light"),
    NativeShadow = 2 UMETA(DisplayName="Native Shadow"),
    ShadowChannel = 3 UMETA(DisplayName="Shadow Channel"),
    AmbientOcclusion = 4 UMETA(DisplayName="Screen Space AO"),
    BaseColor = 5 UMETA(DisplayName="Base Color"),
    WorldNormal = 6 UMETA(DisplayName="World Normal"),
    Roughness = 7 UMETA(DisplayName="Roughness"),
    ShadingModel = 8 UMETA(DisplayName="Shading Model"),
    Depth = 9 UMETA(DisplayName="Depth"),
    Velocity = 10 UMETA(DisplayName="Velocity"),
    // [DeferredToon] Expose split radiance and orientation as distinct semantics.
    SpecularDirectLight = 11 UMETA(DisplayName="Specular Direct Light"),
    DirectLightFacing = 12 UMETA(DisplayName="Direct Light Facing (N dot L)"),
    // [DeferredToon] Scene-linear visible color and material-independent light response.
    EffectiveDirectLight = 13 UMETA(DisplayName="Effective Direct Diffuse"),
    EffectiveLightResponse = 14 UMETA(DisplayName="Effective Light Response"),
    IndirectDiffuse = 15 UMETA(DisplayName="Indirect Diffuse"), // [DeferredToon]
    SkyDiffuseBeforeAO = 16 UMETA(DisplayName="Sky Diffuse Before AO"), // [DeferredToon]
    SkyOcclusion = 17 UMETA(DisplayName="Sky Combined AO"), // [DeferredToon]
    DeprecatedShadowBuffer = 100 UMETA(DisplayName="Deprecated Shadow Buffer"),
};

UENUM(BlueprintType)
enum class EDeferredToonStyleInputChannel : uint8
{
    RGB = 0 UMETA(DisplayName="RGB"),
    R = 1 UMETA(DisplayName="Red"),
    G = 2 UMETA(DisplayName="Green"),
    B = 3 UMETA(DisplayName="Blue"),
    A = 4 UMETA(DisplayName="Alpha"),
    Luminance = 5 UMETA(DisplayName="Luminance"),
};

UENUM(BlueprintType)
enum class EDeferredToonStyleLayerOperation : uint8
{
    Multiply = 0 UMETA(DisplayName="Multiply"),
    Add = 1 UMETA(DisplayName="Add"),
    Replace = 2 UMETA(DisplayName="Replace"),
};

// [DeferredToon] Optional authored stack for Style Layers Preview. The old
// built-in test stack is retired; value 2 now selects an empty neutral stack.
UENUM(BlueprintType)
enum class EDeferredToonStyleSource : uint8
{
    StyleProfile = 0 UMETA(DisplayName="Style Profile"),
    InlineLayers = 1 UMETA(DisplayName="Inline Layers"),
    DefaultStack = 2 UMETA(DisplayName="No Style Layers"),
};

// [DeferredToon] Data-asset layer metadata uses stable numeric IDs so the
// registry can add or retire render targets without changing this asset ABI.
USTRUCT(BlueprintType)
struct ENGINE_API FDeferredToonStyleInputLayer
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Deferred Toon Layer")
    EDeferredToonStyleInputId InputId = EDeferredToonStyleInputId::Unknown;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Deferred Toon Layer")
    EDeferredToonStyleInputChannel Channel = EDeferredToonStyleInputChannel::RGB;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Deferred Toon Layer")
    EDeferredToonStyleLayerOperation Operation = EDeferredToonStyleLayerOperation::Multiply;

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
