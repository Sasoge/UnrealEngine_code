#pragma once

#include "CoreMinimal.h"
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
