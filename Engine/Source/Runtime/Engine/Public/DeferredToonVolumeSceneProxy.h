#pragma once

#include "CoreMinimal.h"
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
    // [DeferredToon] Direct-light band controls are POD snapshots for the
    // renderer; no UObject/component references cross the render boundary.
    FLinearColor DirectLightColor = FLinearColor::White;
    float DirectLightIntensity = 1.0f;
    float DirectLightThreshold = 0.5f;
    float DirectLightSoftness = 0.1f;
    FLinearColor DirectShadowColor = FLinearColor::Black;
    float BlendWeight = 1.0f;
    // [DeferredToon] Snapshot the enum as POD so the render thread never
    // retains a UObject/component reference.
    uint8 OutputMode = 0;
    // [DeferredToon] 255 means use the global console variable; values 1-13
    // are the editor-facing per-volume debug selector.
    uint8 DebugView = 255;
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
    TStaticArray<FLinearColor, CurveSampleCount> ShadowColorSamples;
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
