#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectKey.h"
#include "Engine/DeferredToonInputRegistry.h"
#include "Math/Color.h"

class UDeferredToonVolumeComponent;

// [DeferredToon] Render-thread POD snapshot; no UObject references are retained.
/** Render-thread-owned immutable data for a deferred toon color volume. */
class FDeferredToonVolumeSceneProxy
{
public:
    static constexpr int32 CurveSampleCount = 16;
    ENGINE_API explicit FDeferredToonVolumeSceneProxy(const UDeferredToonVolumeComponent* Component);
    ENGINE_API ~FDeferredToonVolumeSceneProxy();

    void UpdateComponentTransform(const FTransform& Transform);

    const FTransform& GetWorldTransform() const { return WorldTransform; }
    const FTransform& GetInverseTransform() const { return InverseTransform; }
    const FBox& GetWorldBounds() const { return WorldBounds; }
    bool IsEnabled() const { return bEnabled; }
    int32 GetPriority() const { return Priority; }
    int32 GetObjectID() const { return ObjectID; }
    uint32 GetStableSortKey() const { return StableSortKey; }
    const FString& GetStableSortPath() const { return StableSortPath; }

    FTransform WorldTransform;
    FTransform InverseTransform;
    FBox WorldBounds;
    FVector LocalBoxExtent = FVector(1.0f);
    bool bEnabled = true;
    float EdgeFalloff = 0.25f;
    float ShadowColorVolumeWeight = 0.0f;
    float BlendWeight = 1.0f;
    // [DeferredToon] Snapshot the enum as POD so the render thread never
    // retains a UObject/component reference.
    uint8 OutputMode = 0;
    // [DeferredToon] Exact curve endpoints and independent reconstruction POD.
    float BaseColorWeight = 0.0f;
    float ToonShadowThreshold = 0.5f;
    FLinearColor ToonLitColor = FLinearColor::White;
    FLinearColor ToonDarkColor = FLinearColor::Black;
    // [DeferredToon] 255 follows the global console; other values are stable shader IDs.
    uint8 DebugView = 255;
    FObjectKey DebugPreviewKey; // Identity only: never resolved on the render thread.
    // [DeferredToon] Effective selection and color mapping POD/resources.
    float EffectiveLightThreshold = 0.1f;
    // [DeferredToon] Packed, sorted intervals: minimum, value, texture influence, replace weight.
    static constexpr int32 MaxLightBands = 8;
    uint32 LightBandCount = 0;
    TStaticArray<FVector4f, MaxLightBands> LightBandParams;
    TStaticArray<FVector4f, MaxLightBands> LightBandColors;
    float LitColorReplaceWeight = 0.0f;
    float ShadowColorReplaceWeight = 1.0f;
    float OtherLightingWeight = 1.0f;
    // [DeferredToon] Independent direct-specular band; no new render target.
    uint8 DirectSpecularStyle = 0;
    float DirectSpecularThreshold = 0.1f;
    uint8 DirectSpecularColorMode = 0;
    FLinearColor DirectSpecularColor = FLinearColor::White;
    float DirectSpecularIntensity = 1.0f;
    // [DeferredToon] Optional independent indirect diffuse grade.
    bool bOverrideEmissive = false;
    float EmissiveIntensity = 1.0f;
    bool bOverrideIndirectSpecular = false;
    float IndirectSpecularIntensity = 1.0f;
    bool bOverrideIndirectDiffuse = false;
    uint8 IndirectDiffuseStyle = 0;
    float IndirectDiffuseThreshold = 0.1f;
    FLinearColor IndirectDiffuseDarkColor = FLinearColor::Black;
    float IndirectDiffuseIntensity = 1.0f;
    FLinearColor IndirectDiffuseColor = FLinearColor::White;
    float IndirectDiffuseColorReplaceWeight = 0.0f;
    // [DeferredToon] Optional sky split controls.
    bool bOverrideSkyOcclusion = false;
    float SkyOcclusionStrength = 1.0f;
    FLinearColor SkyOcclusionColor = FLinearColor::Black;
    float SkyOcclusionColorReplaceWeight = 0.0f;
    // [DeferredToon] Lumen short-range diffuse AO, independent from sky AO.
    bool bHardIndirectOcclusion = false;
    float IndirectOcclusionThreshold = 0.8f;
    bool bUnifiedIndirectOcclusion = false; // [DeferredToon] Can operate without indirect color grading.
    bool bOverrideLumenShortRangeAO = false;
    float LumenShortRangeAOStrength = 1.0f;
    float ShadowTextureWeight = 1.0f;
    FVector2D ShadowTextureTiling = FVector2D(1.0, 1.0);
    FVector2D ShadowTextureOffset = FVector2D::ZeroVector;
    FTextureRHIRef ShadowTextureResource;
    bool bShadowTextureValid = false;

    // [DeferredToon] Only fixed POD data crosses to the renderer; the asset
    // pointer itself remains owned by the component/editor side.
    bool bUseStyleProfile = false;
    bool bUseInlineStyleLayers = false;
    FDeferredToonInputStack InputStack;
    int32 Priority = 0;
    int32 ObjectID = 0;
    uint32 StableSortKey = 1;
    FString StableSortPath;
    TStaticArray<float, CurveSampleCount> FalloffSamples;
    TStaticArray<FLinearColor, CurveSampleCount> LightingColorSamples;
    FTextureRHIRef ShadowColorVolumeResource;
    bool bShadowColorVolumeValid = false;
};

struct FDeferredToonVolumeView
{
    const FDeferredToonVolumeSceneProxy* Proxy = nullptr;
};

using FDeferredToonVolumeViewArray = TArray<FDeferredToonVolumeView>;

ENGINE_API FDeferredToonVolumeSceneProxy* CreateDeferredToonVolumeSceneProxy(const UDeferredToonVolumeComponent* Component);
ENGINE_API void AddDeferredToonVolumeProxy(class FSceneInterface* Scene, FDeferredToonVolumeSceneProxy* Proxy);
ENGINE_API void RemoveDeferredToonVolumeProxy(class FSceneInterface* Scene, FDeferredToonVolumeSceneProxy* Proxy);
ENGINE_API void UpdateDeferredToonVolumeProxyTransform(FDeferredToonVolumeSceneProxy* Proxy, const FTransform& Transform);
ENGINE_API void DestroyDeferredToonVolumeSceneProxy(FDeferredToonVolumeSceneProxy* Proxy);
